#include "mcp_avplayer_tools.h"
#include "media/media_backend.h"
namespace mcp {
namespace {
std::shared_ptr<MediaBackend> Backend() {
  static auto backend = std::make_shared<MediaBackend>();
  return backend;
}
ToolResult Result(const json &value) {
  ToolResult result;
  result.is_error = !value.value("success", false);
  ContentItem content;
  content.type = "text";
  content.text = value.dump();
  result.content.push_back(std::move(content));
  return result;
}
json Object(json properties = json::object(), json required = json::array()) {
  return {
      {"type", "object"}, {"properties", properties}, {"required", required}};
}
json Ranges() {
  return {{"type", "array"},
          {"items", Object({{"start_ms", {{"type", "integer"}, {"minimum", 0}}},
                            {"end_ms", {{"type", "integer"}, {"minimum", 1}}},
                            {"text", {{"type", "string"}}},
                            {"reason", {{"type", "string"}}}},
                           {"start_ms", "end_ms"})}};
}
void Register(McpServer &server, const std::string &name,
              const std::string &description, const json &schema, bool heavy) {
  Tool tool;
  tool.name = name;
  tool.description = description;
  tool.input_schema.properties = {{"session_id", {{"type", "string"}}},
                                  {"media_id", {{"type", "string"}}},
                                  {"arguments", schema}};
  if (heavy) {
    tool.input_schema.properties["job_id"] = {{"type", "string"}};
    tool.input_schema.properties["timeout_seconds"] = {
        {"type", "integer"}, {"minimum", 1}, {"maximum", 1800}};
  }
  tool.input_schema.required = {"session_id", "media_id", "arguments"};
  if (heavy)
    tool.input_schema.required.push_back("job_id");
  server.RegisterTool(tool, [name, backend = Backend()](const json &args) {
    try {
      return Result(backend->Call(name, args));
    } catch (const std::exception &error) {
      return Result({{"success", false}, {"error", error.what()}});
    }
  });
}
} // namespace
void RegisterAVPlayerTools(McpServer &server) {
  Register(server, "get_media_context",
           "Query uploaded media state through C++", Object(), false);
  Register(server, "get_transcript",
           "Read timestamped subtitles through C++ cache/file/database",
           Object(), false);
  Register(server, "save_transcript",
           "Persist subtitle segments and update cache/database in C++",
           Object({{"segments", Ranges()}}, {"segments"}), false);
  Register(server, "set_edit_plan", "Persist a timestamped edit plan in C++",
           Object({{"segments", Ranges()}}, {"segments"}), false);
  Register(server, "extract_audio",
           "Submit native C++ audio extraction; returns job ID", Object(),
           true);
  auto clip =
      Object({{"start_ms", {{"type", "integer"}, {"minimum", 0}}},
              {"end_ms", {{"type", "integer"}, {"minimum", 1}}},
              {"duration_ms",
               {{"type", "integer"}, {"minimum", 1}, {"maximum", 600000}}}});
  Register(server, "export_video",
           "Submit precise range or random duration clip export to C++ Worker",
           Object({{"start_ms",{{"type","integer"},{"minimum",0}}},{"end_ms",{{"type","integer"},{"minimum",1}}}}, {"start_ms","end_ms"}), true);
  Register(server, "export_random_clip",
           "Export random duration clip; duration_ms required", Object({{"duration_ms",{{"type","integer"},{"minimum",1},{"maximum",600000}}}}, {"duration_ms"}), true);
  Register(server, "export_plan", "Submit batch clip export to C++ Worker",
           Object({{"segments", Ranges()}}, {"segments"}), true);
  Register(server, "transcode_video",
           "Submit H.264/AAC transcoding to C++ Worker",
           Object({{"height",
                    {{"type", "integer"}, {"enum", {360, 480, 720, 1080}}}}},
                  {"height"}),
           true);
  Register(server, "transcribe_subtitle",
           "Submit C++-managed speech task; returns timestamped segments",
           Object({{"provider",
                    {{"type", "string"}, {"enum", {"local", "cloud"}}}}}),
           true);
  for (bool cancel : {false, true}) {
    Tool tool;
    tool.name = cancel ? "avplayer_job_cancel" : "avplayer_job_get";
    tool.description = cancel ? "Cancel an AVPlayer C++ Worker task"
                              : "Query an AVPlayer C++ Worker task";
    tool.input_schema.properties = {{"session_id", {{"type", "string"}}},
                                    {"job_id", {{"type", "string"}}}};
    tool.input_schema.required = {"session_id", "job_id"};
    server.RegisterTool(
        tool, [name = tool.name, backend = Backend()](const json &args) {
          try {
            return Result(backend->Call(name, args));
          } catch (const std::exception &error) {
            return Result({{"success", false}, {"error", error.what()}});
          }
        });
  }
}
} // namespace mcp
