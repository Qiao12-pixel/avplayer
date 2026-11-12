#include "AgentProtocol.h"

namespace av::agent {

QJsonObject ToJson(const AgentMessage& message) {
    QJsonObject object{
        {"protocol", "avplayer.agent.v1"},
        {"type", message.type},
        {"request_id", message.requestId},
        {"session_id", message.sessionId}
    };
    if (!message.tool.isEmpty()) object.insert("tool", message.tool);
    if (!message.arguments.isEmpty()) object.insert("arguments", message.arguments);
    if (!message.payload.isEmpty()) object.insert("payload", message.payload);
    return object;
}

bool FromJson(const QJsonObject& object, AgentMessage* message, QString* error) {
    if (!message) return false;
    if (object.value("protocol").toString() != "avplayer.agent.v1") {
        if (error) *error = "不支持的 Agent 协议版本。";
        return false;
    }
    const QString type = object.value("type").toString().trimmed();
    const QString requestId = object.value("request_id").toString().trimmed();
    if (type.isEmpty() || requestId.isEmpty()) {
        if (error) *error = "Agent 消息缺少 type 或 request_id。";
        return false;
    }
    message->type = type;
    message->requestId = requestId;
    message->sessionId = object.value("session_id").toString();
    message->tool = object.value("tool").toString();
    message->arguments = object.value("arguments").toObject();
    message->payload = object.value("payload").toObject();
    return true;
}

}
