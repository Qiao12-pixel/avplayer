#include "MediaFiles.h"
#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <random>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
extern char **environ;
using namespace avtools;
void process(std::vector<std::string> args, const fs::path &stdoutFile = {}) {
  std::vector<char *> argv;
  for (auto &arg : args)
    argv.push_back(arg.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (!stdoutFile.empty())
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
                                     stdoutFile.c_str(),
                                     O_CREAT | O_WRONLY | O_TRUNC, 0600);
  pid_t pid;
  int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc)
    throw std::runtime_error("cannot execute " + args.front());
  int status;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR)
      throw std::runtime_error("waitpid failed");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    throw std::runtime_error(args.front() + " failed; see execution.log");
}
void ffmpeg(std::vector<std::string> args) {
  args.insert(args.begin(), {"ffmpeg", "-nostdin", "-y", "-loglevel", "error"});
  process(std::move(args));
}
json execute(const json &request, const fs::path &work) {
  auto session = request.at("session_id").get<std::string>(),
       media = request.at("media_id").get<std::string>(),
       job = request.at("job_id").get<std::string>(),
       task = request.at("task").get<std::string>();
  if (!id(job))
    throw std::invalid_argument("invalid job ID");
  auto dir = mediaDirectory(session, media), source = original(dir);
  if (source.empty())
    throw std::runtime_error("uploaded media not found");
  auto stage = work / "staging";
  if (!inside(root(), stage))
    throw std::invalid_argument("job outside storage");
  fs::create_directories(stage);
  auto args = request.value("arguments", json::object());
  auto publish = [&](const fs::path &file, bool clip) {
    auto targetDir = clip ? dir / "clips" : dir;
    if (!inside(root(), targetDir))
      throw std::invalid_argument("artifact directory outside storage");
    fs::create_directories(targetDir);
    auto target = targetDir / (job + "-" + file.filename().string());
    fs::rename(file, target);
    return target.string();
  };
  if (task == "extract_audio" || task == "transcribe_subtitle") {
    auto audio = stage / "audio.mp3";
    ffmpeg({"-i", source.string(), "-vn", "-ac", "1", "-ar", "16000", "-b:a",
            "32k", audio.string()});
    if (fs::file_size(audio) > 20 * 1024 * 1024)
      throw std::runtime_error("audio exceeds 20 MB");
    if (task == "extract_audio")
      return {{"success", true}, {"path", publish(audio, false)}};
    // Inference backend is independent from Agent and never implements media
    // tools.
    auto inference = fs::path(env("MCP_MEDIA_ROOT", ".")) /
                     "src/media/inference/speech.py";
    auto result = stage / "speech.json";
    process({env("AVPLAYER_WORKER_PYTHON", "python3"), inference.string(),
             audio.string(), result.string(), args.value("provider", "local")});
    auto value = readJson(result);
    fs::remove(audio);
    if (!value.value("success", false))
      throw std::runtime_error(value.value("error", "inference failed"));
    return value;
  }
  if (task == "transcode_video") {
    int height = args.value("height", 720);
    if (height != 360 && height != 480 && height != 720 && height != 1080)
      throw std::invalid_argument("height must be 360,480,720,1080");
    auto output = stage / "transcoded.mp4";
    ffmpeg({"-i", source.string(), "-map", "0:v:0", "-map", "0:a:0?", "-vf",
            "scale=-2:" + std::to_string(height), "-c:v", "libx264", "-preset",
            "veryfast", "-c:a", "aac", "-movflags", "+faststart",
            output.string()});
    return {{"success", true}, {"path", publish(output, true)}};
  }
  if (task != "export_video" && task != "export_random_clip" &&
      task != "export_plan")
    throw std::invalid_argument("unknown media tool");
  auto probe = stage / "probe.json";
  process({"ffprobe", "-v", "error", "-show_entries", "format=duration", "-of",
           "json", source.string()},
          probe);
  auto data = readJson(probe);
  int64_t length = static_cast<int64_t>(
      std::stod(data.at("format").at("duration").get<std::string>()) * 1000);
  auto ranges = json::array();
  if (task == "export_plan") {
    ranges = args.value("segments", json::array());
    if (!ranges.is_array() || ranges.empty() || ranges.size() > 5)
      throw std::invalid_argument("plan must contain 1..5 segments");
  } else {
    int64_t start = 0, end = 0;
    if (args.contains("start_ms") || args.contains("end_ms")) {
      start = args.value("start_ms", int64_t(0));
      end = args.value("end_ms", int64_t(0));
    } else {
      auto duration = args.value("duration_ms", int64_t(0));
      if (duration <= 0 || duration > length || duration > 600000)
        throw std::invalid_argument("valid range or duration_ms required");
      std::mt19937_64 random(std::random_device{}());
      start =
          std::uniform_int_distribution<int64_t>(0, length - duration)(random);
      end = start + duration;
    }
    ranges.push_back({{"start_ms", start}, {"end_ms", end}});
  }
  // Validate entire plan before encoding or publishing any clip.
  for (const auto &range : ranges) {
    auto start = range.at("start_ms").get<int64_t>(),
         end = range.at("end_ms").get<int64_t>();
    if (start < 0 || end <= start || end > length || end - start > 600000)
      throw std::invalid_argument("clip range invalid or exceeds ten minutes");
  }
  int index = 0;
  std::vector<fs::path> outputs;
  for (const auto &range : ranges) {
    auto start = range.at("start_ms").get<int64_t>(),
         end = range.at("end_ms").get<int64_t>();
    auto output = stage / ("clip-" + std::to_string(++index) + ".mp4");
    ffmpeg({"-ss", std::to_string(start / 1000.0), "-i", source.string(), "-t",
            std::to_string((end - start) / 1000.0), "-map", "0:v:0", "-map",
            "0:a:0?", "-c:v", "libx264", "-c:a", "aac", "-movflags",
            "+faststart", output.string()});
    outputs.push_back(output);
  }
  if (task == "export_plan") {
    json paths = json::array();
    for (auto &path : outputs)
      paths.push_back(publish(path, true));
    return {{"success", true}, {"paths", paths}, {"count", paths.size()}};
  }
  auto start = ranges[0]["start_ms"].get<int64_t>(),
       end = ranges[0]["end_ms"].get<int64_t>();
  return {{"success", true},
          {"path", publish(outputs.front(), true)},
          {"start_ms", start},
          {"end_ms", end},
          {"duration_ms", end - start}};
}
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  auto parent = std::stol(argv[3]);
  std::thread([parent] {
    for (;;) {
      if (getppid() != parent)
        kill(0, SIGKILL);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }).detach();
  json result;
  try {
    auto request = readJson(argv[1]);
    result = execute(request, fs::path(argv[1]).parent_path());
  } catch (const std::exception &e) {
    result = {{"success", false}, {"error", e.what()}};
  }
  try {
    atomicWrite(argv[2], result.dump());
    return 0;
  } catch (...) {
    return 1;
  }
}
