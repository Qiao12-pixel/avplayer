#include "media_backend.h"
#include "MediaFiles.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fcntl.h>
#include <iomanip>
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
  struct Job {
    json request, status;
    std::atomic<bool> cancel{false};
  };
  std::mutex mutex;
  std::condition_variable cv;
  bool stop = false;
  std::map<std::string, std::shared_ptr<Job>> jobs;
  std::deque<std::shared_ptr<Job>> queue;
  std::vector<std::thread> threads;
  Impl() {
    auto slots = std::stoul(env("MCP_MEDIA_SLOTS", "2"));
    if (!slots || slots > 32)
      throw std::invalid_argument("invalid MCP_MEDIA_SLOTS");
    for (size_t i = 0; i < slots; i++)
      threads.emplace_back([this] { loop(); });
  }
  ~Impl() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
      for (auto &j : jobs)
        j.second->cancel = true;
    }
    cv.notify_all();
    for (auto &t : threads)
      t.join();
  }
  void loop() {
    for (;;) {
      std::shared_ptr<Job> job;
      {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return stop || !queue.empty(); });
        if (stop)
          return;
        job = queue.front();
        queue.pop_front();
        if (job->cancel)
          continue;
        job->status["state"] = "running";
      }
      json result;
      std::string error;
      bool timeout = false;
      try {
        auto work = mediaDirectory(job->request.at("session_id"),
                                   job->request.at("media_id")) /
                    "jobs" / job->request.at("job_id").get<std::string>();
        fs::create_directories(work);
        auto input = work / "request.json", output = work / "result.json";
        atomicWrite(input, job->request.dump());
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
            std::chrono::seconds(job->request.value("timeout_seconds", 600));
        for (;;) {
          auto r = waitpid(pid, &code, WNOHANG);
          if (r == pid)
            break;
          if (r < 0) {
            if (errno == EINTR)
              continue;
            throw std::runtime_error("wait failed");
          }
          if (job->cancel || std::chrono::steady_clock::now() > deadline) {
            timeout = !job->cancel;
            kill(-pid, SIGKILL);
            while (waitpid(pid, &code, 0) < 0 && errno == EINTR) {
            }
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!timeout && !job->cancel) {
          if (!WIFEXITED(code) || WEXITSTATUS(code))
            throw std::runtime_error("media executor failed");
          result = readJson(output);
          if (!result.value("success", false))
            error = result.value("error", "media failed");
        }
      } catch (const std::exception &e) {
        error = e.what();
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        auto state = job->cancel                 ? "cancelled"
                     : timeout || !error.empty() ? "failed"
                                                 : "succeeded";
        job->status["state"] = state;
        job->status["success"] = std::string(state) == "succeeded";
        job->status["error"] = timeout ? "execution timeout" : error;
        if (state == std::string("succeeded"))
          job->status["result"] = result;
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
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (tool == "avplayer_job_get" || tool == "avplayer_job_cancel") {
    auto it = impl->jobs.find(args.at("job_id"));
    if (it == impl->jobs.end() || it->second->request["session_id"] != session)
      return {{"success", false}, {"error", "job not found"}};
    auto job = it->second;
    if (tool == "avplayer_job_cancel" && (job->status["state"] == "queued" ||
                                          job->status["state"] == "running")) {
      job->cancel = true;
      if (job->status["state"] == "queued") {
        job->status["state"] = "cancelled";
        job->status["success"] = false;
        impl->queue.erase(
            std::remove(impl->queue.begin(), impl->queue.end(), job),
            impl->queue.end());
      }
    }
    return job->status;
  }
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
  auto existing = impl->jobs.find(jobID);
  if (existing != impl->jobs.end()) {
    if (existing->second->request != request)
      throw std::invalid_argument("job_id input conflict");
    return existing->second->status;
  }
  if (impl->queue.size() >= 16 || impl->jobs.size() >= 4096)
    throw std::runtime_error("MCP media queue/retained limit reached");
  if (fs::exists(dir / "jobs" / jobID / "request.json"))
    throw std::runtime_error(
        "execution record exists; explicit recovery required");
  auto job = std::make_shared<Impl::Job>();
  job->request = request;
  job->status = {{"success", true},
                 {"job_id", jobID},
                 {"state", "queued"},
                 {"worker", "mcp-local"},
                 {"error", ""}};
  impl->jobs[jobID] = job;
  impl->queue.push_back(job);
  impl->cv.notify_one();
  return job->status;
}
} // namespace mcp
