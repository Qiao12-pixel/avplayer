#ifndef AV_AGENT_AGENTBRIDGE_H
#define AV_AGENT_AGENTBRIDGE_H

#include "AgentProtocol.h"

#include <QProcess>
#include <QObject>

namespace av::agent {

class AgentBridge final : public QObject {
    Q_OBJECT
public:
    explicit AgentBridge(QObject* parent = nullptr);
    ~AgentBridge() override;

    void SetPythonProgram(const QString& program);
    void SetRuntimeScript(const QString& script);
    void SetWorkingDirectory(const QString& directory);
    bool Start();
    void Stop();
    bool IsRunning() const { return m_process.state() != QProcess::NotRunning; }
    void SubmitGoal(const QString& goal, const QString& sessionId);
    void SendToolCatalog(const QJsonArray& tools);
    void SendToolResult(const QString& requestId, const QString& sessionId,
                        const QString& tool, const QJsonObject& result);

signals:
    void runtimeStarted();
    void runtimeStopped(int exitCode);
    void messageReceived(const av::agent::AgentMessage& message);
    void protocolError(const QString& message);

private:
    void ReadLines();
    bool Send(const AgentMessage& message);

    QProcess m_process;
    QString m_pythonProgram{"python3"};
    QString m_runtimeScript;
    QString m_workingDirectory;
};

}

#endif // AV_AGENT_AGENTBRIDGE_H
