#ifndef AV_AGENT_TOOLDISPATCHER_H
#define AV_AGENT_TOOLDISPATCHER_H

#include "IAgentTool.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>

namespace av::agent {

class ToolDispatcher final {
public:
    bool Register(AgentToolDefinition definition, QString* error = nullptr);
    QJsonObject Dispatch(const QString& name, const QJsonObject& arguments) const;
    QJsonArray Definitions() const;
    bool Contains(const QString& name) const { return m_tools.contains(name); }

private:
    QHash<QString, AgentToolDefinition> m_tools;
};

}

#endif // AV_AGENT_TOOLDISPATCHER_H
