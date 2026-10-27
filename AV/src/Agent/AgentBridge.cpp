#include "AgentBridge.h"

#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QUuid>
#include <exception>

namespace av::agent {

AgentBridge::AgentBridge(QObject* parent) : QObject(parent) {
    qRegisterMetaType<AgentMessage>("av::agent::AgentMessage");
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AgentBridge::ReadLines);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        emit protocolError(QString("Agent Runtime 进程错误（%1）：%2").arg(error).arg(m_process.errorString()));
    });
    connect(&m_process, &QProcess::started, this, &AgentBridge::runtimeStarted);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus) { emit runtimeStopped(code); });
}

AgentBridge::~AgentBridge() { Stop(); }

void AgentBridge::SetPythonProgram(const QString& program) { m_pythonProgram = program.trimmed(); }
void AgentBridge::SetRuntimeScript(const QString& script) { m_runtimeScript = script; }
void AgentBridge::SetWorkingDirectory(const QString& directory) { m_workingDirectory = directory; }

bool AgentBridge::Start() {
    if (IsRunning()) return true;
    if (m_runtimeScript.isEmpty()) {
        emit protocolError("未设置 Agent Runtime 脚本路径。");
        return false;
    }
    if (!m_workingDirectory.isEmpty()) m_process.setWorkingDirectory(m_workingDirectory);
    m_process.start(m_pythonProgram, {m_runtimeScript});
    return m_process.waitForStarted(1500);
}

void AgentBridge::Stop() {
    if (!IsRunning()) return;
    m_process.closeWriteChannel();
    m_process.terminate();
    if (!m_process.waitForFinished(1000)) m_process.kill();
}

void AgentBridge::SubmitGoal(const QString& goal, const QString& sessionId) {
    Send({"goal", QUuid::createUuid().toString(QUuid::WithoutBraces), sessionId, {}, {}, {{"goal", goal}}});
}

void AgentBridge::SendToolCatalog(const QJsonArray& tools) {
    Send({"tool_catalog", QUuid::createUuid().toString(QUuid::WithoutBraces), {}, {}, {}, {{"tools", tools}}});
}

void AgentBridge::SendToolResult(const QString& requestId, const QString& sessionId,
                                 const QString& tool, const QJsonObject& result) {
    Send({"tool_result", requestId, sessionId, tool, {}, result});
}

bool AgentBridge::Send(const AgentMessage& message) {
    if (!IsRunning()) {
        emit protocolError("Agent Runtime 尚未启动。");
        return false;
    }
    const QByteArray line = QJsonDocument(ToJson(message)).toJson(QJsonDocument::Compact) + '\n';
    return m_process.write(line) == line.size();
}

void AgentBridge::ReadLines() {
    while (m_process.canReadLine()) {
        const QByteArray line = m_process.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit protocolError("Agent Runtime 返回了无效 JSON：" + QString::fromUtf8(line));
            continue;
        }
        AgentMessage message;
        QString error;
        if (!FromJson(document.object(), &message, &error)) emit protocolError(error);
        else emit messageReceived(message);
    }
}

}
