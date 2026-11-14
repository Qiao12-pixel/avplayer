#include "mcp_builtin_prompts.h"

namespace mcp {
    void RegisterBuiltinPrompts(McpServer& mcp) {
        Prompt prompt;
        prompt.name = "code_review";
        prompt.description = "Generate code review prompt";
        prompt.arguments.emplace_back(PromptArgument{
            .name = "code",
            .required = true,
        });
        prompt.arguments.emplace_back(PromptArgument{
            .name = "language",
            .required = true,
        });

        mcp.RegisterPrompt(prompt, [](const json& args) -> std::vector<PromptMessage> {
            std::vector<PromptMessage> pmsgs;
            PromptMessage pmsg;
            pmsg.role = Role::User;
            pmsg.content = {
                {"type", "text"},
                {"text", "Please review this " + args.at("language").get<std::string>() +
                         " code:\n\n" + args.at("code").get<std::string>()}
            };
            pmsgs.emplace_back(pmsg);
            return pmsgs;
        });
    }
}
