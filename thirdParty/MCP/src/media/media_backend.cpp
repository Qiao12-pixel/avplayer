#include "media_backend.h"
#include "MediaFiles.h"
#include "messaging/Brokers.h"
#include "messaging/JobStore.h"
#include <atomic>
#include <fcntl.h>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
extern char **environ;
namespace mcp {
using namespace avtools;
struct MediaBackend::Impl {
  messaging::JobStore store;
  std::mutex artifacts;
  std::atomic<bool> stopping{false};
  std::vector<std::thread> consumers;
  std::thread publisher, events;
  Impl() {
    auto slots = std::stoul(env("MCP_MEDIA_SLOTS", "2"));
    if (!slots || slots > 32)
      throw std::invalid_argument("invalid MCP_MEDIA_SLOTS");
    publisher = std::thread([this] { publishTasks(); });
    events = std::thread([this] { publishEvents(); });
    for (size_t i = 0; i < slots; ++i)
      consumers.emplace_back([this] { consume(); });
  }
  ~Impl() {
    stopping = true;
    for (auto &thread : consumers)
      thread.join();
    publisher.join();
    events.join();
  }
  void pause() {
    for (int i = 0; i < 10 && !stopping; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  void publishTasks() {
    while (!stopping) {
      try {
        messaging::Rabbit broker;
        while (!stopping) {
          auto tasks = store.Pending();
          for (auto &id : tasks) {
            if (stopping)
              break;
            messaging::Rabbit destination(id.at("topic"));
            destination.Publish(id.at("key"));
            store.Published(id);
          }
          pause();
        }
      } catch (const std::exception &e) {
        std::cerr << "[RabbitMQ publisher] " << e.what() << std::endl;
        pause();
      }
    }
  }
  void publishEvents() {
    while (!stopping) {
      try {
        messaging::Kafka kafka;
        while (!stopping) {
          for (auto &event : store.Events()) {
            if (stopping)
              break;
            kafka.Publish(event.at("key"), event.at("payload"),
                          event.at("topic"));
            store.EventSent(event);
          }
          pause();
        }
      } catch (const std::exception &e) {
        std::cerr << "[Kafka outbox] " << e.what() << std::endl;
        pause();
      }
    }
  }
  json execute(const json &request, const std::function<bool()> &cancelled) {
    json result;
    std::string error;
    bool timeout = false;
    try {
      auto work =
          mediaDirectory(request.at("session_id"), request.at("media_id")) /
          "jobs" / request.at("job_id").get<std::string>() /
          ("attempt-" +
           std::to_string(request.at("execution_version").get<uint64_t>()));
      fs::create_directories(work);
      auto input = work / "request.json", output = work / "result.json";
      atomicWrite(input, request.dump());
      std::vector<std::string> args = {
          env("MCP_MEDIA_EXECUTOR", "mcp_media_executor"), input.string(),
          output.string(), std::to_string(getpid())};
      std::vector<char *> argv;
      for (auto &arg : args)
        argv.push_back(arg.data());
      argv.push_back(nullptr);
      posix_spawnattr_t attr;
      posix_spawnattr_init(&attr);
      posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
      posix_spawnattr_setpgroup(&attr, 0);
      posix_spawn_file_actions_t actions;
      posix_spawn_file_actions_init(&actions);
      auto log = work / "execution.log";
      posix_spawn_file_actions_addopen(&actions, 1, log.c_str(),
                                       O_WRONLY | O_CREAT | O_TRUNC, 0600);
      posix_spawn_file_actions_adddup2(&actions, 1, 2);
      pid_t pid;
      auto rc =
          posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), environ);
      posix_spawnattr_destroy(&attr);
      posix_spawn_file_actions_destroy(&actions);
      if (rc)
        throw std::runtime_error("cannot start MCP_MEDIA_EXECUTOR");
      int code;
      auto deadline =
          std::chrono::steady_clock::now() +
          std::chrono::seconds(request.value("timeout_seconds", 600));
      for (;;) {
        auto r = waitpid(pid, &code, WNOHANG);
        if (r == pid)
          break;
        if (r < 0) {
          if (errno == EINTR)
            continue;
          throw std::runtime_error("wait failed");
        }
        if (stopping || cancelled() ||
            std::chrono::steady_clock::now() > deadline) {
          timeout = !cancelled() && !stopping;
          kill(-pid, SIGKILL);
          while (waitpid(pid, &code, 0) < 0 && errno == EINTR) {
          }
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (!stopping && !timeout && !cancelled()) {
        if (!WIFEXITED(code) || WEXITSTATUS(code))
          throw std::runtime_error("media executor failed");
        result = readJson(output);
        if (!result.value("success", false))
          error = result.value("error", "media failed");
      }
    } catch (const std::exception &e) {
      error = e.what();
    }
    if (stopping)
      return nullptr;
    if (cancelled())
      return {{"success", false},
              {"error", "cancelled by caller"},
              {"cancelled", true}};
    if (timeout)
      return {{"success", false}, {"error", "execution timeout"}};
    if (!error.empty())
      return {{"success", false}, {"error", error}};
    return result;
  }
  void consume() {
    while (!stopping) {
      try {
        messaging::Rabbit broker;
        while (!stopping) {
          auto delivery = broker.Fetch();
          if (delivery.id.empty()) {
            pause();
            continue;
          }
          if (!id(delivery.id)) {
            broker.Reject(delivery.tag);
            continue;
          }
          auto request = store.Claim(delivery.id);
          if (request.is_null()) {
            broker.Ack(delivery.tag);
            continue;
          }
          if (request.value("busy", false)) {
            broker.Requeue(delivery.tag);
            pause();
            continue;
          }
          if (request.value("exhausted", false)) {
            broker.Reject(delivery.tag);
            continue;
          }
          auto version = request.at("execution_version").get<uint64_t>();
          auto nextRenew = std::chrono::steady_clock::now();
          bool lost = false;
          auto valid = [&] {
            if (lost)
              return false;
            if (std::chrono::steady_clock::now() < nextRenew)
              return true;
            try {
              lost = !store.Renew(delivery.id, version);
            } catch (...) {
              lost = true;
            }
            nextRenew = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(std::max(
                            500, std::stoi(env("MCP_JOB_LEASE_SECONDS", "15")) *
                                     1000 / 3));
            return !lost;
          };
          auto result = execute(request, [&] { return !valid(); });
          if (stopping)
            break;
          auto state = result.value("cancelled", false) ? "cancelled"
                       : result.value("success", false) ? "succeeded"
                                                        : "failed";
          json status = {{"success", std::string(state) == "succeeded"},
                         {"job_id", delivery.id},
                         {"state", state},
                         {"worker", "rabbitmq-consumer"},
                         {"error", result.value("error", "")}};
          if (result.value("success", false))
            status["result"] = result;
          const auto completion = store.Complete(delivery.id, version, status);
          if (completion == messaging::JobStore::Completion::stale ||
              completion == messaging::JobStore::Completion::retry_queued) {
            broker.Ack(delivery.tag);
            continue;
          }
          if (completion == messaging::JobStore::Completion::exhausted)
            broker.Reject(delivery.tag);
          else
            broker.Ack(delivery.tag);
        }
      } catch (const std::exception &e) {
        std::cerr << "[RabbitMQ consumer] " << e.what() << std::endl;
        pause();
      }
    }
  }
};
MediaBackend::MediaBackend() : impl(std::make_unique<Impl>()) {}
MediaBackend::~MediaBackend() = default;
json MediaBackend::Call(const std::string &tool, const json &args) {
  auto session = args.at("session_id").get<std::string>();
  if (!id(session))
    throw std::invalid_argument("invalid session");
  if (tool == "avplayer_job_get" || tool == "avplayer_job_cancel")
    return impl->store.Get(args.at("job_id"), session,
                           tool == "avplayer_job_cancel");
  std::lock_guard<std::mutex> lock(impl->artifacts);
  auto media = args.at("media_id").get<std::string>();
  auto dir = mediaDirectory(session, media);
  auto arguments = args.value("arguments", json::object());
  if (!arguments.is_object())
    throw std::invalid_argument("arguments must be object");
  if (tool == "get_media_context")
    return {
        {"success", true}, {"loaded", !original(dir).empty()}, {"path", media}};
  if (tool == "get_transcript") {
    json segments = json::array();
    if (fs::exists(dir / "transcript.json"))
      segments = readJson(dir / "transcript.json");
    std::string text;
    for (auto &s : segments) {
      if (!text.empty())
        text += '\n';
      text += "[" + std::to_string(s.at("start_ms").get<int64_t>()) + "-" +
              std::to_string(s.at("end_ms").get<int64_t>()) + "] " +
              s.at("text").get<std::string>();
    }
    return {{"success", true},
            {"text", text},
            {"has_timestamps", !segments.empty()},
            {"source_path", media}};
  }
  if (tool == "save_transcript" || tool == "set_edit_plan") {
    if (original(dir).empty())
      throw std::invalid_argument("media not uploaded");
    auto segments = arguments.at("segments");
    if (!segments.is_array() || segments.size() > 100000)
      throw std::invalid_argument("invalid segments");
    for (auto &s : segments)
      if (s.at("start_ms").get<int64_t>() < 0 ||
          s.at("end_ms").get<int64_t>() <= s.at("start_ms").get<int64_t>())
        throw std::invalid_argument("invalid segment range");
    if (tool == "set_edit_plan") {
      atomicWrite(dir / "edit_plan.json", segments.dump());
      return {{"success", true}};
    }
    auto stamp = [](int64_t ms) {
      std::ostringstream out;
      out << std::setfill('0') << std::setw(2) << ms / 3600000 << ":"
          << std::setw(2) << ms / 60000 % 60 << ":" << std::setw(2)
          << ms / 1000 % 60 << "," << std::setw(3) << ms % 1000;
      return out.str();
    };
    std::string srt;
    int i = 0;
    for (auto &s : segments)
      srt += std::to_string(++i) + "\n" + stamp(s.at("start_ms")) + " --> " +
             stamp(s.at("end_ms")) + "\n" + s.at("text").get<std::string>() +
             "\n\n";
    atomicWrite(dir / "transcript.json", segments.dump());
    atomicWrite(dir / "transcript.srt", srt);
    return {{"success", true}, {"path", (dir / "transcript.srt").string()}};
  }
  auto jobID = args.at("job_id").get<std::string>();
  if (!id(jobID))
    throw std::invalid_argument("invalid job_id");
  auto timeout = args.value("timeout_seconds", 600);
  if (timeout < 1 || timeout > 1800)
    throw std::invalid_argument("invalid timeout");
  json request = {{"job_id", jobID},        {"session_id", session},
                  {"media_id", media},      {"task", tool},
                  {"arguments", arguments}, {"timeout_seconds", timeout}};
  return impl->store.Submit(request);
}
} // namespace mcp
