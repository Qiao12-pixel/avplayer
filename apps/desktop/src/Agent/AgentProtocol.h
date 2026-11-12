#ifndef AV_AGENT_AGENTPROTOCOL_H
#define AV_AGENT_AGENTPROTOCOL_H

#include <QJsonObject>
#include <QString>
#include <QMetaType>

namespace av::agent {

struct AgentMessage {
    QString type;
    QString requestId;
    QString sessionId;
    QString tool;
    QJsonObject arguments;
    QJsonObject payload;
};

QJsonObject ToJson(const AgentMessage& message);
bool FromJson(const QJsonObject& object, AgentMessage* message, QString* error = nullptr);

}

Q_DECLARE_METATYPE(av::agent::AgentMessage)

#endif // AV_AGENT_AGENTPROTOCOL_H
