#include "ToolDispatcher.h"

namespace av::agent {

bool ToolDispatcher::Register(AgentToolDefinition definition, QString* error) {
    definition.name = definition.name.trimmed();
    if (definition.name.isEmpty() || !definition.handler) {
        if (error) *error = "工具必须包含 name 和 handler。";
        return false;
    }
    if (m_tools.contains(definition.name)) {
        if (error) *error = "工具已注册：" + definition.name;
        return false;
    }
    m_tools.insert(definition.name, std::move(definition));
    return true;
}

QJsonObject ToolDispatcher::Dispatch(const QString& name, const QJsonObject& arguments) const {
    const auto it = m_tools.constFind(name);
    if (it == m_tools.cend()) return QJsonObject{{"success", false}, {"error", "未知工具：" + name}};
    try {
        QJsonObject result = it->handler(arguments);
        result.insert("success", result.value("success").toBool(true));
        return result;
    } catch (const std::exception& exception) {
        return QJsonObject{{"success", false}, {"error", QString::fromUtf8(exception.what())}};
    } catch (...) {
        return QJsonObject{{"success", false}, {"error", "工具执行发生未知异常。"}};
    }
}

QJsonArray ToolDispatcher::Definitions() const {
    QJsonArray definitions;
    for (const auto& tool : m_tools) {
        definitions.append(QJsonObject{
            {"name", tool.name},
            {"description", tool.description},
            {"input_schema", tool.inputSchema}
        });
    }
    return definitions;
}

}
