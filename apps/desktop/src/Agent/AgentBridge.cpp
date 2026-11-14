#include "AgentBridge.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>

#include <atomic>
#include <mutex>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "agent.grpc.pb.h"

namespace av::agent {

namespace {

using avplayer::agent::v1::AgentService;
using avplayer::agent::v1::ClientMessage;
using avplayer::agent::v1::MediaChunk;
using avplayer::agent::v1::MediaHandle;
using avplayer::agent::v1::MediaService;
using avplayer::agent::v1::ServerMessage;
using avplayer::agent::v1::Struct;

Struct ToStruct(const QJsonObject& object) {
    Struct value;
    if (!object.isEmpty())
        value.set_json(QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString());
    return value;
}

QJsonObject FromStruct(const Struct& value) {
    if (value.json().empty()) return {};
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(value.json()), &error);
    return error.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject{};
}

ClientMessage ToClientMessage(const AgentMessage& message) {
    ClientMessage proto;
    proto.set_type(message.type.toStdString());
    proto.set_request_id(message.requestId.toStdString());
    proto.set_session_id(message.sessionId.toStdString());
    if (!message.tool.isEmpty()) proto.set_tool(message.tool.toStdString());
    *proto.mutable_payload() = ToStruct(message.payload);
    *proto.mutable_arguments() = ToStruct(message.arguments);
    return proto;
}

AgentMessage ToAgentMessage(const ServerMessage& proto) {
    AgentMessage message;
    message.type = QString::fromStdString(proto.type());
    message.requestId = QString::fromStdString(proto.request_id());
    message.sessionId = QString::fromStdString(proto.session_id());
    message.tool = QString::fromStdString(proto.tool());
    message.payload = FromStruct(proto.payload());
    message.arguments = FromStruct(proto.arguments());
    return message;
}

} // namespace

namespace {

// Upload is intentionally independent of AgentBridgeWorker: it uses its own
// channel/stub so it can run on a QtConcurrent worker while the UI thread
// owns the Converse stream.
bool UploadMediaWithStub(MediaService::Stub& stub, const QString& filePath,
                         const QString& sessionId, QString* mediaId, QString* error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = "无法打开待上传文件：" + filePath;
        return false;
    }
    grpc::ClientContext context;
    MediaHandle handle;
    std::unique_ptr<grpc::ClientWriter<MediaChunk>> writer(stub.UploadMedia(&context, &handle));

    MediaChunk first;
    first.set_session_id(sessionId.toStdString());
    first.set_file_name(QFileInfo(filePath).fileName().toStdString());
    if (!writer->Write(first)) {
        if (error) *error = "上传流已关闭。";
        return false;
    }

    constexpr qint64 kChunkSize = 1 << 20;
    while (!file.atEnd()) {
        const QByteArray bytes = file.read(kChunkSize);
        MediaChunk chunk;
        chunk.set_data(bytes.constData(), bytes.size());
        if (!writer->Write(chunk)) break;
    }
    writer->WritesDone();
    const grpc::Status status = writer->Finish();
    if (!status.ok()) {
        if (error) *error = QString::fromStdString(status.error_message());
        return false;
    }
    if (mediaId) *mediaId = QString::fromStdString(handle.media_id());
    return true;
}

} // namespace

class AgentBridgeWorker {
public:
    AgentBridgeWorker(AgentBridge* owner, std::shared_ptr<grpc::Channel> channel)
        : m_owner(owner), m_channel(std::move(channel)),
          m_agentStub(AgentService::NewStub(m_channel)),
          m_mediaStub(MediaService::NewStub(grpc::CreateChannel(qEnvironmentVariable("AVPLAYER_DATA_SERVER", "localhost:50066").toStdString(), grpc::InsecureChannelCredentials()))) {}

    ~AgentBridgeWorker() { Stop(); }

    bool Start() {
        m_context = std::make_unique<grpc::ClientContext>();
        m_stream = m_agentStub->Converse(m_context.get());
        if (!m_stream) return false;
        m_running = true;
        m_readerThread = std::thread([this] { ReadLoop(); });
        return true;
    }

    void Stop() {
        // Always join the reader thread, even if the Converse stream already
        // ended on its own. Returning early here leaves a joinable std::thread
        // that terminates the process when the worker is destroyed.
        const bool wasRunning = m_running.exchange(false);
        {
            std::lock_guard lock(m_writeMutex);
            if (wasRunning && m_stream) m_stream->WritesDone();
        }
        if (m_context) m_context->TryCancel();
        if (m_readerThread.joinable()) m_readerThread.join();
        if (m_stream) m_stream->Finish();
        m_stream.reset();
        m_context.reset();
    }

    bool IsRunning() const { return m_running; }

    bool Send(const ClientMessage& message) {
        if (!m_running) return false;
        std::lock_guard lock(m_writeMutex);
        return m_stream->Write(message);
    }

    bool UploadMedia(const QString& filePath, const QString& sessionId, QString* mediaId, QString* error) {
        return UploadMediaWithStub(*m_mediaStub, filePath, sessionId, mediaId, error);
    }

private:
    void ReadLoop() {
        emit_owner_started();
        ServerMessage response;
        while (m_running && m_stream->Read(&response)) {
            emit m_owner->messageReceived(ToAgentMessage(response));
        }
        m_running = false;
        emit m_owner->runtimeStopped(0);
    }

    void emit_owner_started() { emit m_owner->runtimeStarted(); }

    AgentBridge* m_owner;
    std::shared_ptr<grpc::Channel> m_channel;
    std::unique_ptr<AgentService::Stub> m_agentStub;
    std::unique_ptr<MediaService::Stub> m_mediaStub;
    std::unique_ptr<grpc::ClientContext> m_context;
    std::unique_ptr<grpc::ClientReaderWriter<ClientMessage, ServerMessage>> m_stream;
    std::thread m_readerThread;
    std::mutex m_writeMutex;
    std::atomic<bool> m_running{false};
};

AgentBridge::AgentBridge(QObject* parent) : QObject(parent) {
    qRegisterMetaType<AgentMessage>("av::agent::AgentMessage");
}

AgentBridge::~AgentBridge() { Stop(); }

void AgentBridge::SetServerAddress(const QString& address) { m_serverAddress = address; }
void AgentBridge::SetDefaultProvider(const QString& provider) {
    m_defaultProvider = provider == "cloud" ? "cloud" : "local";
}

bool AgentBridge::Start() {
    if (m_worker) {
        if (m_worker->IsRunning()) return true;
        // Stop and destroy the previous worker explicitly instead of letting
        // unique_ptr replacement run its destructor while the old Converse
        // stream may still be winding down.
        m_worker->Stop();
        m_worker.reset();
    }
    auto channel = grpc::CreateChannel(m_serverAddress.toStdString(), grpc::InsecureChannelCredentials());
    m_worker = std::make_unique<AgentBridgeWorker>(this, channel);
    if (!m_worker->Start()) {
        emit protocolError("无法连接 Agent 服务端：" + m_serverAddress);
        m_worker.reset();
        return false;
    }
    return true;
}

void AgentBridge::Stop() {
    if (!m_worker) return;
    Send({"shutdown", QUuid::createUuid().toString(QUuid::WithoutBraces), {}, {}, {}, {}});
    m_worker.reset();
}

bool AgentBridge::IsRunning() const { return m_worker && m_worker->IsRunning(); }

bool AgentBridge::UploadMediaToServer(const QString& serverAddress, const QString& filePath,
                                      const QString& sessionId, QString* mediaId, QString* error) {
    auto channel = grpc::CreateChannel(qEnvironmentVariable("AVPLAYER_DATA_SERVER", "localhost:50066").toStdString(), grpc::InsecureChannelCredentials());
    Q_UNUSED(serverAddress);
    auto stub = MediaService::NewStub(channel);
    return UploadMediaWithStub(*stub, filePath, sessionId, mediaId, error);
}

bool AgentBridge::UploadMedia(const QString& filePath, const QString& sessionId,
                               QString* mediaId, QString* error) {
    return UploadMediaToServer(m_serverAddress, filePath, sessionId, mediaId, error);
}

void AgentBridge::SubmitGoal(const QString& goal, const QString& sessionId) {
    Send({"goal", QUuid::createUuid().toString(QUuid::WithoutBraces), sessionId, {}, {}, {{"goal", goal}}});
}

void AgentBridge::SubmitTask(const QString& task, const QJsonObject& arguments, const QString& sessionId) {
    Send({"task", QUuid::createUuid().toString(QUuid::WithoutBraces), sessionId, {}, arguments,
          {{"task", task}}});
}

void AgentBridge::SetProvider(const QString& provider) {
    Send({"provider", QUuid::createUuid().toString(QUuid::WithoutBraces), "runtime", {}, {},
          {{"provider", provider}}});
}

void AgentBridge::SendToolCatalog(const QJsonArray& tools) {
    Send({"tool_catalog", QUuid::createUuid().toString(QUuid::WithoutBraces), {}, {}, {}, {{"tools", tools}}});
}

void AgentBridge::SendToolResult(const QString& requestId, const QString& sessionId,
                                 const QString& tool, const QJsonObject& result) {
    Send({"tool_result", requestId, sessionId, tool, {}, result});
}

void AgentBridge::Send(const AgentMessage& message) {
    if (!m_worker) {
        emit protocolError("Agent 客户端尚未连接。");
        return;
    }
    if (!m_worker->Send(ToClientMessage(message)))
        emit protocolError("发送 Agent 消息失败：" + message.type);
}

}
