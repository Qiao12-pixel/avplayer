#ifndef AV_AGENT_AGENTBRIDGE_H
#define AV_AGENT_AGENTBRIDGE_H

#include "AgentProtocol.h"

#include <QObject>
#include <QString>

#include <memory>

namespace av::agent {

// Internal gRPC plumbing (stream reader thread, Converse call). Kept out of
// this header so callers don't need to see the generated protobuf/gRPC
// headers.
class AgentBridgeWorker;

// Talks to the AgentService/MediaService exposed by services/agent over
// gRPC. The desktop app never spawns or imports Python: orchestration and
// storage belong to the agent deployment unit. The public API
// (Submit*/Send*/signals) is unchanged so MainWindow only wires transport.
class AgentBridge final : public QObject {
    Q_OBJECT
public:
    explicit AgentBridge(QObject* parent = nullptr);
    ~AgentBridge() override;

    // host:port of the agent gRPC server, e.g. "localhost:50061".
    void SetServerAddress(const QString& address);
    const QString& ServerAddress() const { return m_serverAddress; }
    void SetDefaultProvider(const QString& provider);
    bool Start();
    void Stop();
    bool IsRunning() const;

    // Streams a local file to MediaService.UploadMedia and records the
    // returned media_id as the active media for `sessionId` server-side.
    // Blocking (run off the UI thread for large files); returns false and
    // fills `error` on failure. `mediaId`, when provided, receives the
    // server-assigned id (needed to negotiate playback via StreamingService).
    bool UploadMedia(const QString& filePath, const QString& sessionId,
                     QString* mediaId = nullptr, QString* error = nullptr);

    // Thread-safe upload helper: opens its own channel/stub instead of using
    // the Converse worker, so it can run on a QtConcurrent thread without
    // touching this object's lifetime.
    static bool UploadMediaToServer(const QString& serverAddress, const QString& filePath,
                                    const QString& sessionId, QString* mediaId = nullptr,
                                    QString* error = nullptr);

    void SubmitGoal(const QString& goal, const QString& sessionId);
    void SubmitTask(const QString& task, const QJsonObject& arguments, const QString& sessionId);
    void SetProvider(const QString& provider);
    void SendToolCatalog(const QJsonArray& tools);
    void SendToolResult(const QString& requestId, const QString& sessionId,
                        const QString& tool, const QJsonObject& result);

signals:
    void runtimeStarted();
    void runtimeStopped(int exitCode);
    void messageReceived(const av::agent::AgentMessage& message);
    void protocolError(const QString& message);

private:
    void Send(const AgentMessage& message);

    QString m_serverAddress{"localhost:50061"};
    QString m_defaultProvider{"local"};
    std::unique_ptr<AgentBridgeWorker> m_worker;
};

}

#endif // AV_AGENT_AGENTBRIDGE_H
