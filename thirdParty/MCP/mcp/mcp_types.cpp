/**
 * @file mcp_types.cpp
 * @brief 
 * @author Joe
 * @date 26-4-29
 */


#include "mcp_types.h"


namespace mcp {
    json ToolInputSchema::to_json() const {
        json j = {
            {"type", type},
            {"properties", properties}
        };
        if (!required.empty()) {
            j["required"] = required;
        }
        return j;
    }
    ToolInputSchema ToolInputSchema::from_json(const json &j) {
        ToolInputSchema schema;
        schema.type = j.value("type", "object");
        schema.properties = j.value("properties", json::object());

        if (j.contains("required")) {
            schema.required = j["required"].get<std::vector<std::string>>();
        }
        return schema;
    }

    json Tool::to_json() const {
        return {
            {"name", name},
            {"description", description},
            {"inputSchema", input_schema.to_json()}
        };
    }
    Tool Tool::from_json(const json &j) {
        Tool tool;
        tool.name = j.value("name", "");
        tool.description = j.value("description", "");
        tool.input_schema = ToolInputSchema::from_json(j.value("inputSchema", json::object()));
        return tool;
    }
    json ContentItem::to_json() const {
        json j = {
            {"type", type}
        };
        if (text.has_value()) {
            j["text"] = text.value();
        } if (data.has_value()) {
            j["data"] = data.value();
        } if (mime_type.has_value()) {
            j["mimeType"] = mime_type.value();
        } if (url.has_value()) {
            j["url"] = url.value();
        }
        return j;
    }
    ContentItem ContentItem::from_json(const json &j) {
        ContentItem content;
        content.type = j.value("type", "object");
        if (j.contains("text")) {
            content.text = j.value("text", "");
        } if (j.contains("data")) {
            content.data = j.value("data", "");
        } if (j.contains("mimeType")) {
            content.mime_type = j.value("mimeType", "");
        } if (j.contains("url")) {
            content.url = j.value("url", "");
        }
        return content;
    }

    json ToolResult::to_json() const {
        json content_arr = json::array();
        for (const auto &item : content) {
            content_arr.push_back(item.to_json());
        }
        json j = {{"content", content_arr}};
        if (is_error) {
            j["isError"] = is_error;
        }
        return j;
    }
    ToolResult ToolResult::from_json(const json &j) {
        ToolResult result;
        if (j.contains("content")) {
            for (const auto& item : j["content"]) {
                result.content.push_back(ContentItem::from_json(item));
            }
        }
        result.is_error = j.value("isError", false);
        return result;
    }
    json Resource::to_json() const {
        json j = {
            {"url", url},
            {"name", name}
        };
        if (description.has_value()) {
            j["description"] = description.value();
        } if (mime_type.has_value()) {
            j["mimeType"] = mime_type.value();
        }
        return j;
    }
    Resource Resource::from_json(const json &j) {
        Resource resource;
        resource.url = j.value("url", "");
        resource.name = j.value("name", "");
        if (j.contains("description")) {
            resource.description = j.value("description", "");
        } if (j.contains("mimeType")) {
            resource.mime_type = j.value("mimeType", "");
        }
        return resource;
    }
    json ResourceContent::to_json() const {
        json j = {
            {"url", url},
            {"text", text}
        };
        if (mime_type.has_value()) {
            j["mimeType"] = mime_type.value();
        } if (blob.has_value()) {
            j["blob"] = blob.value();
        }
        return j;
    }
    ResourceContent ResourceContent::from_json(const json &j) {
        ResourceContent resource;
        resource.url = j.value("url", "");
        resource.text = j.value("text", "");
        if (j.contains("mimeType")) {
            resource.mime_type = j.value("mimeType", "");
        } if (j.contains("blob")) {
            resource.blob = j.value("blob", "");
        }
        return resource;
    }
    json PromptArgument::to_json() const {
        json j = {{"name", name}, {"required", required}};
        if (description.has_value()) {
            j["description"] = description.value();
        }
        return j;
    }
    PromptArgument PromptArgument::from_json(const json &j) {
        PromptArgument prompt_argument;
        prompt_argument.name = j.value("name", "");
        prompt_argument.required = j.value("required", false);
        if (j.contains("description")) {
            prompt_argument.description = j.value("description", "");
        }
        return prompt_argument;
    }
    json Prompt::to_json() const {
        json j = {{"name", name}};
        if (description.has_value()) {
            j["description"] = description.value();
        }
        if (!arguments.empty()) {
            json args_arr = json::array();
            for (const auto &argument : arguments) {
                args_arr.push_back(argument.to_json());
            }
            j["arguments"] = args_arr;
        }
        return j;
    }
    Prompt Prompt::from_json(const json &j) {
        Prompt prompt;
        prompt.name = j.value("name", "");
        if (j.contains("description")) {
            prompt.description = j.value("description", "");
        }
        if (j.contains("arguments")) {
            for (const auto& argument : j["arguments"]) {
                prompt.arguments.push_back(PromptArgument::from_json(argument));
            }
        }
        return prompt;
    }
    json PromptMessage::to_json() const {
        return {
            {"role", role == Role::User ? "user" : "assistant"},
            {"content", content}
        };
    }
    PromptMessage PromptMessage::from_json(const json &j) {
        PromptMessage msg;
        std::string role_str = j.at("role").get<std::string>();
        msg.role = (role_str == "user") ? Role::User : Role::Assistant;
        msg.content = j.at("content");
        return msg;
    }
    json ServerCapabilities::ToolsCapability::to_json() const {
        return {{"listChanged", list_changed}};
    }
    ServerCapabilities::ToolsCapability ServerCapabilities::ToolsCapability::from_json(const json &j) {
        ToolsCapability tools_capability;
        tools_capability.list_changed = j.value("listChanged", false);
        return tools_capability;

    }
    json ServerCapabilities::ResourceCapability::to_json() const {
        return {{"subscribe", subscribe}, {"listChanged", list_changed}};
    }
    ServerCapabilities::ResourceCapability ServerCapabilities::ResourceCapability::from_json(const json &j) {
        ResourceCapability resource_capability;
        resource_capability.subscribe = j.value("subscribe", false);
        resource_capability.list_changed = j.value("listChanged", false);
        return resource_capability;
    }
    json ServerCapabilities::PromptCapability::to_json() const {
        return {{"listChanged", list_changed}};
    }
    ServerCapabilities::PromptCapability ServerCapabilities::PromptCapability::from_json(const json &j) {
        PromptCapability prompt_capability;
        prompt_capability.list_changed = j.value("listChanged", false);
        return prompt_capability;
    }
    json ServerCapabilities::to_json() const {
        json j = json::object();
        if (tools.has_value()) {
            j["tools"] = tools->to_json();
        } if (resources.has_value()) {
            j["resources"] = resources->to_json();
        } if (prompts.has_value()) {
            j["prompts"] = prompts->to_json();
        } if (logging.has_value()) {
            j["logging"] = logging.value();
        }
        return j;
    }
    ServerCapabilities ServerCapabilities::from_json(const json &j) {
        ServerCapabilities server_cap;
        if (j.contains("tools")) {
            server_cap.tools = ToolsCapability::from_json(j.value("tools", json::object()));
        } if (j.contains("resources")) {
            server_cap.resources = ResourceCapability::from_json(j.value("resources", json::object()));
        } if (j.contains("prompts")) {
            server_cap.prompts = PromptCapability::from_json(j.value("prompts", json::object()));
        } if (j.contains("logging")) {
            server_cap.logging = j.value("logging", json::object());
        }
        return server_cap;
    }

    json ServerInfo::to_json() const {
        return {{"name", name}, {"version", version}};
    }

    ServerInfo ServerInfo::from_json(const json &j) {
        ServerInfo server_info;
        server_info.name = j.value("name", "");
        server_info.version = j.value("version", "");
        return server_info;
    }
    json InitializeResult::to_json() const {
        json j = {{"protocolVersion",protocol_version}};
        j["capabilities"] = server_cap.to_json();
        j["serverInfo"] = server_info.to_json();
        return j;
    }
    InitializeResult InitializeResult::from_json(const json &j) {
        InitializeResult initialize_result;
        initialize_result.protocol_version = j.value("protocolVersion", std::string(DEFAULT_NEGOTIATED_VERSION));
        initialize_result.server_cap = ServerCapabilities::from_json(j.value("capabilities", json::object()));
        initialize_result.server_info = ServerInfo::from_json(j.value("serverInfo", json::object()));
        return initialize_result;
    }

}
