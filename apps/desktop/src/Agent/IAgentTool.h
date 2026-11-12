#ifndef AV_AGENT_IAGENTTOOL_H
#define AV_AGENT_IAGENTTOOL_H

#include <QJsonObject>
#include <QString>

#include <functional>

namespace av::agent {

struct AgentToolDefinition {
    QString name;
    QString description;
    QJsonObject inputSchema;
    std::function<QJsonObject(const QJsonObject&)> handler;
};

}

#endif // AV_AGENT_IAGENTTOOL_H
