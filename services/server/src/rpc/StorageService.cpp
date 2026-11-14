#include "StorageService.h"
#include <set>
grpc::Status StorageService::Execute(grpc::ServerContext *,
                                     const avplayer::agent::v1::Struct *request,
                                     avplayer::agent::v1::Struct *response) {
  try {
    auto value = nlohmann::json::parse(request->json());
    auto action = value.at("action").get<std::string>();
    if (action == "result_get" || action == "result_put") {
      std::lock_guard<std::mutex> lock(resultMutex);
      auto session = value.at("session_id").get<std::string>(),
           media = value.at("media_id").get<std::string>();
      auto task = value.at("task").get<std::string>(),
           variant = value.value("variant", "");
      auto valid = [](const std::string &id, size_t max) {
        return !id.empty() && id.size() <= max &&
               id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP"
                                    "QRSTUVWXYZ0123456789-_") ==
                   std::string::npos;
      };
      if (!valid(session, 64) || !valid(media, 32) ||
          (!variant.empty() && !valid(variant, 64)) ||
          (task != "transcribe_subtitle" && task != "summarize_video"))
        return {grpc::StatusCode::INVALID_ARGUMENT,
                "invalid result identity or kind"};
      if (action == "result_put") {
        auto payload = value.at("payload");
        if (!payload.is_object() || !payload.value("success", false) ||
            payload.dump().size() > 4 * 1024 * 1024)
          return {grpc::StatusCode::INVALID_ARGUMENT,
                  "only successful bounded results can be stored"};
        if (task == "transcribe_subtitle") {
          auto segments = payload.at("segments");
          if (!segments.is_array() || segments.empty())
            throw std::invalid_argument("subtitle segments required");
          std::string text;
          for (const auto &segment : segments) {
            auto start = segment.at("start_ms").get<int64_t>(),
                 end = segment.at("end_ms").get<int64_t>();
            if (start < 0 || end <= start)
              throw std::invalid_argument("invalid subtitle range");
            if (!text.empty())
              text += '\n';
            text += "[" + std::to_string(start) + "-" + std::to_string(end) +
                    "] " + segment.at("text").get<std::string>();
          }
          payload["text"] = text;
          payload["source_path"] = media;
          payload["has_timestamps"] = true;
        } else if (payload.value("result", "").empty())
          throw std::invalid_argument("summary must not be empty");
        value["payload"] = payload;
        bool saved = repository.Execute(value) == true;
        auto cached = value;
        cached["action"] = "result_cache_put";
        bool hit = cache.Execute(cached) == true;
        // Refresh legacy subtitle tables/cache as well for existing data
        // consumers.
        if (task == "transcribe_subtitle") {
          auto legacy = value;
          legacy["segments"] = payload["segments"];
          legacy["action"] = "save_transcript";
          repository.Execute(legacy);
          legacy["action"] = "cache_put";
          cache.Execute(legacy);
        }
        response->set_json(nlohmann::json{
            {"mysql_saved", saved},
            {"redis_cached",
             hit}}.dump());
      } else {
        auto cached = value;
        cached["action"] = "result_cache_get";
        auto result = cache.Execute(cached);
        if (!result.is_object()) {
          result = repository.Execute(value);
          if (result.is_object()) {
            cached["action"] = "result_cache_put";
            cached["payload"] = result;
            cache.Execute(cached);
          }
        }
        response->set_json(result.dump());
      }
      return grpc::Status::OK;
    }
    static const std::set<std::string> cacheActions = {"cache_get", "cache_put",
                                                       "cache_drop"};
    static const std::set<std::string> sqlActions = {
        "record_session",  "record_media",    "record_task",
        "save_transcript", "load_transcript", "list_media"};
    if (cacheActions.count(action))
      response->set_json(cache.Execute(value).dump());
    else if (sqlActions.count(action))
      response->set_json(repository.Execute(value).dump());
    else
      return {grpc::StatusCode::INVALID_ARGUMENT, "unknown storage operation"};
    return grpc::Status::OK;
  } catch (const std::exception &error) {
    return {grpc::StatusCode::INVALID_ARGUMENT, error.what()};
  }
}
