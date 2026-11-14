#include "mcp_builtin_tools.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <tuple>
#include <unistd.h>
#include <vector>

#include <curl/curl.h>

#include "config/config.h"
#include "sql/tool_call_history_repository.h"

namespace mcp {
    namespace {
        ToolResult MakeTextResult(const std::string& text, bool is_error = false) {
            ToolResult result;
            result.is_error = is_error;
            result.content.emplace_back(ContentItem{
                .type = "text",
                .text = text
            });
            return result;
        }

        std::string UrlEncode(const std::string& value) {
            std::ostringstream escaped;
            escaped << std::uppercase << std::hex << std::setfill('0');

            for (const unsigned char ch : value) {
                if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                    escaped << ch;
                } else if (ch == ' ') {
                    escaped << "%20";
                } else {
                    escaped << '%' << std::setw(2) << static_cast<int>(ch);
                }
            }
            return escaped.str();
        }

        size_t WriteResponseBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
            auto* response_body = static_cast<std::string*>(userdata);
            response_body->append(ptr, size * nmemb);
            return size * nmemb;
        }

        json GetJsonFromPublicApi(const std::string& url) {
            static const int curl_global_status = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (curl_global_status != CURLE_OK) {
                throw std::runtime_error("curl global init failed");
            }

            CURL* curl = curl_easy_init();
            if (curl == nullptr) {
                throw std::runtime_error("curl init failed");
            }

            std::string response_body;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponseBody);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

            const CURLcode code = curl_easy_perform(curl);
            long http_status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            curl_easy_cleanup(curl);

            if (code != CURLE_OK) {
                throw std::runtime_error(std::string("request failed: ") + curl_easy_strerror(code));
            }
            if (http_status != 200) {
                throw std::runtime_error("request failed with HTTP status " + std::to_string(http_status));
            }
            return json::parse(response_body);
        }

        std::string WeatherCodeToText(int code) {
            switch (code) {
                case 0:
                    return "晴";
                case 1:
                    return "大部晴朗";
                case 2:
                    return "局部多云";
                case 3:
                    return "阴";
                case 45:
                case 48:
                    return "雾";
                case 51:
                case 53:
                case 55:
                    return "毛毛雨";
                case 56:
                case 57:
                    return "冻毛毛雨";
                case 61:
                case 63:
                case 65:
                    return "雨";
                case 66:
                case 67:
                    return "冻雨";
                case 71:
                case 73:
                case 75:
                    return "雪";
                case 77:
                    return "雪粒";
                case 80:
                case 81:
                case 82:
                    return "阵雨";
                case 85:
                case 86:
                    return "阵雪";
                case 95:
                    return "雷暴";
                case 96:
                case 99:
                    return "雷暴伴冰雹";
                default:
                    return "未知天气";
            }
        }

        std::string OptionalLocationField(const json& location, const std::string& key) {
            if (!location.contains(key) || location[key].is_null()) {
                return "";
            }
            return location[key].get<std::string>();
        }

        struct ImageSizeConfig {
            std::string aspect_ratio;
            std::string image_size;
        };

        ImageSizeConfig ResolveImageSizeConfig(const std::string& size) {
            if (size == "2K") {
                return {.aspect_ratio = "1:1", .image_size = "2K"};
            }
            if (size == "4K") {
                return {.aspect_ratio = "1:1", .image_size = "4K"};
            }
            if (size == "512x512" || size == "1024x1024") {
                return {.aspect_ratio = "1:1", .image_size = "1K"};
            }
            if (size == "1024x1792") {
                return {.aspect_ratio = "9:16", .image_size = "1K"};
            }
            if (size == "1792x1024") {
                return {.aspect_ratio = "16:9", .image_size = "1K"};
            }
            throw std::runtime_error("unsupported image size: " + size);
        }

        std::string GetImageFileExtension(const std::string& mime_type) {
            if (mime_type == "image/jpeg" || mime_type == "image/jpg") {
                return ".jpg";
            }
            if (mime_type == "image/webp") {
                return ".webp";
            }
            return ".png";
        }

        bool HasImageFileExtension(const std::string& filename) {
            const auto dot = filename.find_last_of('.');
            if (dot == std::string::npos) {
                return false;
            }
            auto ext = filename.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp";
        }

        std::string ValidateAndNormalizeImageFilename(const std::string& filename,
                                                      const std::string& mime_type) {
            if (filename.empty()) {
                throw std::runtime_error("filename cannot be empty");
            }
            if (filename == "." || filename == ".." ||
                filename.find('/') != std::string::npos ||
                filename.find('\\') != std::string::npos ||
                filename.find("..") != std::string::npos) {
                throw std::runtime_error("filename must be a simple file name under generated/images");
            }
            for (const unsigned char ch : filename) {
                if (!std::isalnum(ch) && ch != '_' && ch != '-' && ch != '.') {
                    throw std::runtime_error("filename may only contain letters, numbers, '.', '_' and '-'");
                }
            }
            if (HasImageFileExtension(filename)) {
                return filename;
            }
            return filename + GetImageFileExtension(mime_type);
        }

        std::vector<unsigned char> DecodeBase64(const std::string& input) {
            static const std::string chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::vector<int> decode_table(256, -1);
            for (size_t i = 0; i < chars.size(); ++i) {
                decode_table[static_cast<unsigned char>(chars[i])] = static_cast<int>(i);
            }

            std::vector<unsigned char> output;
            int val = 0;
            int valb = -8;
            for (const unsigned char ch : input) {
                if (std::isspace(ch)) {
                    continue;
                }
                if (ch == '=') {
                    break;
                }
                const int decoded = decode_table[ch];
                if (decoded == -1) {
                    throw std::runtime_error("invalid base64 image data");
                }
                val = (val << 6) + decoded;
                valb += 6;
                if (valb >= 0) {
                    output.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
                    valb -= 8;
                }
            }
            return output;
        }

        json PostJsonToGemini(const std::string& model,
                              const std::string& api_key,
                              const json& request_body_json) {
            static const int curl_global_status = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (curl_global_status != CURLE_OK) {
                throw std::runtime_error("curl global init failed");
            }

            CURL* curl = curl_easy_init();
            if (curl == nullptr) {
                throw std::runtime_error("curl init failed");
            }

            const std::string url =
                "https://generativelanguage.googleapis.com/v1beta/models/" +
                model + ":generateContent";
            const std::string request_body = request_body_json.dump();
            std::string response_body;

            struct curl_slist* headers = nullptr;
            const std::string api_key_header = "x-goog-api-key: " + api_key;
            headers = curl_slist_append(headers, api_key_header.c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponseBody);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

            const CURLcode code = curl_easy_perform(curl);
            long http_status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (code != CURLE_OK) {
                throw std::runtime_error(std::string("Gemini request failed: ") + curl_easy_strerror(code));
            }
            if (http_status < 200 || http_status >= 300) {
                throw std::runtime_error("Gemini request failed with HTTP status " +
                                         std::to_string(http_status) + ": " + response_body);
            }

            return json::parse(response_body);
        }

        json PostJsonToDoubao(const std::string& api_url,
                              const std::string& api_key,
                              const json& request_body_json) {
            static const int curl_global_status = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (curl_global_status != CURLE_OK) {
                throw std::runtime_error("curl global init failed");
            }

            CURL* curl = curl_easy_init();
            if (curl == nullptr) {
                throw std::runtime_error("curl init failed");
            }

            const std::string request_body = request_body_json.dump();
            std::string response_body;

            struct curl_slist* headers = nullptr;
            const std::string auth_header = "Authorization: Bearer " + api_key;
            headers = curl_slist_append(headers, auth_header.c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponseBody);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

            const CURLcode code = curl_easy_perform(curl);
            long http_status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (code != CURLE_OK) {
                throw std::runtime_error(std::string("Doubao request failed: ") + curl_easy_strerror(code));
            }
            if (http_status < 200 || http_status >= 300) {
                throw std::runtime_error("Doubao request failed with HTTP status " +
                                         std::to_string(http_status) + ": " + response_body);
            }

            return json::parse(response_body);
        }

        std::pair<std::string, std::string> ExtractGeminiInlineImage(const json& response) {
            if (!response.contains("candidates") || !response["candidates"].is_array()) {
                throw std::runtime_error("Gemini response does not contain candidates");
            }
            for (const auto& candidate : response["candidates"]) {
                if (!candidate.contains("content")) {
                    continue;
                }
                const auto& content = candidate["content"];
                if (!content.contains("parts") || !content["parts"].is_array()) {
                    continue;
                }
                for (const auto& part : content["parts"]) {
                    const json* inline_data = nullptr;
                    if (part.contains("inlineData")) {
                        inline_data = &part["inlineData"];
                    } else if (part.contains("inline_data")) {
                        inline_data = &part["inline_data"];
                    }
                    if (inline_data == nullptr || !inline_data->contains("data")) {
                        continue;
                    }

                    std::string mime_type = "image/png";
                    if (inline_data->contains("mimeType")) {
                        mime_type = inline_data->value("mimeType", std::string("image/png"));
                    } else if (inline_data->contains("mime_type")) {
                        mime_type = inline_data->value("mime_type", std::string("image/png"));
                    }
                    return {inline_data->at("data").get<std::string>(), mime_type};
                }
            }
            throw std::runtime_error("Gemini response did not include inline image data");
        }

        std::pair<std::string, std::string> ExtractDoubaoBase64Image(const json& response) {
            if (!response.contains("data") || !response["data"].is_array() || response["data"].empty()) {
                throw std::runtime_error("Doubao response does not contain data array");
            }

            const auto& first_image = response["data"].front();
            if (first_image.contains("b64_json") && first_image["b64_json"].is_string()) {
                return {first_image["b64_json"].get<std::string>(), "image/png"};
            }
            if (first_image.contains("b64Json") && first_image["b64Json"].is_string()) {
                return {first_image["b64Json"].get<std::string>(), "image/png"};
            }
            if (first_image.contains("url")) {
                throw std::runtime_error("Doubao returned an image URL, but b64_json was expected");
            }
            throw std::runtime_error("Doubao response did not include b64_json image data");
        }

        std::string ToLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }

        std::filesystem::path SaveGeneratedImage(const std::string& filename,
                                                 const std::string& mime_type,
                                                 const std::vector<unsigned char>& image_bytes) {
            const std::filesystem::path output_dir = std::filesystem::path("generated") / "images";
            std::filesystem::create_directories(output_dir);

            const auto normalized_filename = ValidateAndNormalizeImageFilename(filename, mime_type);
            const std::filesystem::path output_path = output_dir / normalized_filename;

            std::ofstream file(output_path, std::ios::binary);
            if (!file.is_open()) {
                throw std::runtime_error("failed to open image file for writing: " + output_path.string());
            }
            file.write(reinterpret_cast<const char*>(image_bytes.data()),
                       static_cast<std::streamsize>(image_bytes.size()));
            if (!file.good()) {
                throw std::runtime_error("failed to write image file: " + output_path.string());
            }
            return output_path;
        }

        bool IsPathWithinRoot(const std::filesystem::path& root,
                              const std::filesystem::path& candidate) {
            auto root_it = root.begin();
            auto candidate_it = candidate.begin();
            for (; root_it != root.end(); ++root_it, ++candidate_it) {
                if (candidate_it == candidate.end() || *root_it != *candidate_it) {
                    return false;
                }
            }
            return true;
        }

        std::filesystem::path ResolveWorkspacePath(const std::string& path_text) {
            if (path_text.empty()) {
                throw std::runtime_error("path cannot be empty");
            }

            const std::filesystem::path input_path(path_text);
            if (input_path.is_absolute()) {
                throw std::runtime_error("absolute paths are not allowed");
            }

            const auto workspace_root = std::filesystem::current_path().lexically_normal();
            const auto resolved_path = (workspace_root / input_path).lexically_normal();
            if (!IsPathWithinRoot(workspace_root, resolved_path)) {
                throw std::runtime_error("path must stay within the current workspace");
            }
            return resolved_path;
        }

        std::string RelativeDisplayPath(const std::filesystem::path& path) {
            const auto workspace_root = std::filesystem::current_path().lexically_normal();
            return std::filesystem::relative(path, workspace_root).generic_string();
        }

        std::string ReadTextFile(const std::filesystem::path& path,
                                 std::size_t max_chars,
                                 bool* truncated) {
            if (truncated == nullptr) {
                throw std::runtime_error("truncated output flag is required");
            }

            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                throw std::runtime_error("failed to open file: " + path.string());
            }

            const auto file_size = std::filesystem::file_size(path);
            const auto bytes_to_read =
                static_cast<std::size_t>(std::min<std::uintmax_t>(file_size, max_chars));

            std::string content(bytes_to_read, '\0');
            file.read(content.data(), static_cast<std::streamsize>(bytes_to_read));
            content.resize(static_cast<std::size_t>(file.gcount()));

            if (content.find('\0') != std::string::npos) {
                throw std::runtime_error("binary files are not supported");
            }

            *truncated = file_size > max_chars;
            return content;
        }

        std::vector<std::string> SplitLines(const std::string& content) {
            std::vector<std::string> lines;
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(line);
            }
            if (lines.empty() && !content.empty()) {
                lines.push_back(content);
            }
            return lines;
        }

        std::string Lowercase(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }

        json BuildReadFileJson(const std::filesystem::path& resolved_path,
                               std::size_t max_chars) {
            bool truncated = false;
            const auto content = ReadTextFile(resolved_path, max_chars, &truncated);
            return {
                {"path", RelativeDisplayPath(resolved_path)},
                {"truncated", truncated},
                {"content", content}
            };
        }

        struct LineDiffOp {
            enum class Type {
                Equal,
                Added,
                Removed
            };

            Type type;
            int line_a = 0;
            int line_b = 0;
            std::string text;
        };

        std::vector<LineDiffOp> BuildLineDiff(const std::vector<std::string>& lines_a,
                                              const std::vector<std::string>& lines_b) {
            const std::size_t n = lines_a.size();
            const std::size_t m = lines_b.size();
            const std::uint64_t cell_count =
                static_cast<std::uint64_t>(n + 1) * static_cast<std::uint64_t>(m + 1);
            if (cell_count > 4000000ULL) {
                throw std::runtime_error("compare_files input is too large for line diff");
            }

            std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
            for (std::size_t i = n; i-- > 0;) {
                for (std::size_t j = m; j-- > 0;) {
                    if (lines_a[i] == lines_b[j]) {
                        dp[i][j] = dp[i + 1][j + 1] + 1;
                    } else {
                        dp[i][j] = std::max(dp[i + 1][j], dp[i][j + 1]);
                    }
                }
            }

            std::vector<LineDiffOp> operations;
            std::size_t i = 0;
            std::size_t j = 0;
            while (i < n || j < m) {
                if (i < n && j < m && lines_a[i] == lines_b[j]) {
                    operations.push_back(LineDiffOp{
                        .type = LineDiffOp::Type::Equal,
                        .line_a = static_cast<int>(i + 1),
                        .line_b = static_cast<int>(j + 1),
                        .text = lines_a[i]
                    });
                    ++i;
                    ++j;
                } else if (j < m && (i == n || dp[i][j + 1] > dp[i + 1][j])) {
                    operations.push_back(LineDiffOp{
                        .type = LineDiffOp::Type::Added,
                        .line_a = 0,
                        .line_b = static_cast<int>(j + 1),
                        .text = lines_b[j]
                    });
                    ++j;
                } else if (i < n) {
                    operations.push_back(LineDiffOp{
                        .type = LineDiffOp::Type::Removed,
                        .line_a = static_cast<int>(i + 1),
                        .line_b = 0,
                        .text = lines_a[i]
                    });
                    ++i;
                }
            }
            return operations;
        }

        std::string QuoteShellArgument(const std::string& value) {
            if (value.empty()) {
                return "''";
            }
            std::string quoted = "'";
            for (const char ch : value) {
                if (ch == '\'') {
                    quoted += "'\\''";
                } else {
                    quoted.push_back(ch);
                }
            }
            quoted.push_back('\'');
            return quoted;
        }

        std::string TruncateOutput(const std::string& text, std::size_t max_chars, bool* truncated) {
            if (truncated == nullptr) {
                throw std::runtime_error("truncated output flag is required");
            }
            if (text.size() <= max_chars) {
                *truncated = false;
                return text;
            }
            *truncated = true;
            return text.substr(0, max_chars);
        }

        bool HasAllowedSuffix(const std::filesystem::path& path, const std::vector<std::string>& suffixes) {
            if (suffixes.empty()) {
                return true;
            }
            const auto extension = Lowercase(path.extension().string());
            for (const auto& suffix : suffixes) {
                if (extension == Lowercase(suffix)) {
                    return true;
                }
            }
            return false;
        }

        std::filesystem::path ResolvePathWithinRoot(const std::filesystem::path& workspace_root,
                                                    const std::filesystem::path& base_path,
                                                    const std::string& path_text) {
            if (path_text.empty()) {
                throw std::runtime_error("path cannot be empty");
            }

            const std::filesystem::path input_path(path_text);
            if (input_path.is_absolute()) {
                throw std::runtime_error("absolute paths are not allowed");
            }

            const auto resolved_path = (base_path / input_path).lexically_normal();
            if (!IsPathWithinRoot(workspace_root, resolved_path)) {
                throw std::runtime_error("path must stay within the current workspace");
            }
            return resolved_path;
        }

        struct CommandExecutionResult {
            int exit_code = 0;
            bool timed_out = false;
            bool output_truncated = false;
            std::string output;
        };

        CommandExecutionResult ExecuteAllowlistedCommand(const std::string& executable,
                                                        const std::vector<std::string>& arguments,
                                                        const std::filesystem::path& cwd,
                                                        int timeout_seconds,
                                                        std::size_t max_output_chars) {
            int pipe_fds[2];
            if (pipe(pipe_fds) != 0) {
                throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
            }

            const pid_t pid = fork();
            if (pid < 0) {
                close(pipe_fds[0]);
                close(pipe_fds[1]);
                throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
            }

            if (pid == 0) {
                close(pipe_fds[0]);
                dup2(pipe_fds[1], STDOUT_FILENO);
                dup2(pipe_fds[1], STDERR_FILENO);
                close(pipe_fds[1]);

                if (chdir(cwd.string().c_str()) != 0) {
                    std::fprintf(stderr, "chdir failed: %s\n", std::strerror(errno));
                    _exit(127);
                }

                std::vector<char*> argv;
                argv.reserve(arguments.size() + 2);
                argv.push_back(const_cast<char*>(executable.c_str()));
                for (const auto& argument : arguments) {
                    argv.push_back(const_cast<char*>(argument.c_str()));
                }
                argv.push_back(nullptr);

                execv(executable.c_str(), argv.data());
                std::fprintf(stderr, "exec failed: %s\n", std::strerror(errno));
                _exit(127);
            }

            close(pipe_fds[1]);
            const int flags = fcntl(pipe_fds[0], F_GETFL, 0);
            fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);

            CommandExecutionResult result;
            const auto started = std::chrono::steady_clock::now();
            std::array<char, 512> buffer{};
            bool child_running = true;

            while (child_running) {
                while (true) {
                    const auto bytes_read = read(pipe_fds[0], buffer.data(), buffer.size());
                    if (bytes_read > 0) {
                        if (result.output.size() < max_output_chars) {
                            const auto remaining = max_output_chars - result.output.size();
                            const auto append_count = static_cast<std::size_t>(
                                std::min<ssize_t>(bytes_read, static_cast<ssize_t>(remaining))
                            );
                            result.output.append(buffer.data(), append_count);
                            if (append_count < static_cast<std::size_t>(bytes_read)) {
                                result.output_truncated = true;
                            }
                        } else {
                            result.output_truncated = true;
                        }
                        continue;
                    }
                    if (bytes_read == 0) {
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    close(pipe_fds[0]);
                    kill(pid, SIGKILL);
                    waitpid(pid, nullptr, 0);
                    throw std::runtime_error(std::string("read failed: ") + std::strerror(errno));
                }

                int status = 0;
                const pid_t wait_result = waitpid(pid, &status, WNOHANG);
                if (wait_result == pid) {
                    if (WIFEXITED(status)) {
                        result.exit_code = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        result.exit_code = 128 + WTERMSIG(status);
                    }
                    child_running = false;
                    break;
                }
                if (wait_result < 0) {
                    close(pipe_fds[0]);
                    kill(pid, SIGKILL);
                    waitpid(pid, nullptr, 0);
                    throw std::runtime_error(std::string("waitpid failed: ") + std::strerror(errno));
                }

                const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - started
                );
                if (elapsed.count() >= timeout_seconds) {
                    result.timed_out = true;
                    kill(pid, SIGKILL);
                    waitpid(pid, &status, 0);
                    result.exit_code = 124;
                    child_running = false;
                    break;
                }

                usleep(10000);
            }

            while (true) {
                const auto bytes_read = read(pipe_fds[0], buffer.data(), buffer.size());
                if (bytes_read > 0) {
                    if (result.output.size() < max_output_chars) {
                        const auto remaining = max_output_chars - result.output.size();
                        const auto append_count = static_cast<std::size_t>(
                            std::min<ssize_t>(bytes_read, static_cast<ssize_t>(remaining))
                        );
                        result.output.append(buffer.data(), append_count);
                        if (append_count < static_cast<std::size_t>(bytes_read)) {
                            result.output_truncated = true;
                        }
                    } else {
                        result.output_truncated = true;
                    }
                    continue;
                }
                break;
            }

            close(pipe_fds[0]);
            return result;
        }

    }

    void RegisterBuiltinTools(McpServer& mcp) {
        {
            Tool tool;
            tool.name = "echo";
            tool.description = "Echo back the input message";
            tool.input_schema.properties = {
                {"message", {{"type", "string"}, {"description", "Message to echo"}}}
            };
            tool.input_schema.required = {"message"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                ToolResult result;
                result.content.emplace_back(ContentItem{
                    .type = "text",
                    .text = "echo: " + args["message"].get<std::string>()
                });
                return result;
            });
        }

        {
            Tool tool;
            tool.name = "calculate";
            tool.description = "Perform basic arithmetic operations";
            tool.input_schema.properties = {
                {"operation", {{"type", "string"}, {"enum", json::array({"add", "subtract", "multiply", "divide", "modulo"})}}},
                {"a", {{"type", "number"}}},
                {"b", {{"type", "number"}}}
            };
            tool.input_schema.required = {"operation", "a", "b"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                std::string opera = args["operation"].get<std::string>();
                double a = args["a"].get<double>();
                double b = args["b"].get<double>();
                double value = 0;
                if (opera == "add") {
                    value = a + b;
                } else if (opera == "subtract") {
                    value = a - b;
                } else if (opera == "multiply") {
                    value = a * b;
                } else if (opera == "divide") {
                    if (b == 0) {
                        return MakeTextResult("divide by zero", true);
                    }
                    value = a / b;
                } else {
                    return MakeTextResult("unsupported operation: " + opera, true);
                }
                return MakeTextResult(std::to_string(value));
            });
        }

        {
            Tool tool;
            tool.name = "get_time";
            tool.description = "Get the current system time";
            tool.input_schema.properties = json::object();

            mcp.RegisterTool(tool, [](const json& /*args*/) -> ToolResult {
                std::time_t now = std::time(nullptr);
                char buffer[32];
                std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
                ToolResult result;
                result.content.emplace_back(ContentItem{
                    .type = "text",
                    .text = buffer
                });
                return result;
            });
        }

        {
            Tool tool;
            tool.name = "get_weather";
            tool.description = "Get current weather for a city using the public Open-Meteo API";
            tool.input_schema.properties = {
                {"city", {{"type", "string"}, {"description", "City name (e.g., Beijing, Shanghai, 北京, 上海)"}}}
            };
            tool.input_schema.required = {"city"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    if (!args.contains("city") || !args["city"].is_string()) {
                        return MakeTextResult("city 参数必须是字符串", true);
                    }

                    const auto city = args["city"].get<std::string>();
                    if (city.empty()) {
                        return MakeTextResult("city 参数不能为空", true);
                    }

                    const auto geocoding = GetJsonFromPublicApi(
                        "https://geocoding-api.open-meteo.com/v1/search?name=" + UrlEncode(city) +
                        "&count=1&language=zh&format=json"
                    );

                    if (!geocoding.contains("results") || geocoding["results"].empty()) {
                        return MakeTextResult("未找到城市: " + city, true);
                    }

                    const auto& location = geocoding["results"].front();
                    const double latitude = location.at("latitude").get<double>();
                    const double longitude = location.at("longitude").get<double>();

                    std::ostringstream weather_path;
                    weather_path << "https://api.open-meteo.com/v1/forecast"
                                 << "?latitude=" << latitude
                                 << "&longitude=" << longitude
                                 << "&current=temperature_2m,relative_humidity_2m,precipitation,weather_code,wind_speed_10m,wind_direction_10m"
                                 << "&timezone=auto";

                    const auto forecast = GetJsonFromPublicApi(weather_path.str());
                    const auto& current = forecast.at("current");
                    const auto& units = forecast.value("current_units", json::object());

                    const auto location_name = OptionalLocationField(location, "name");
                    const auto admin1 = OptionalLocationField(location, "admin1");
                    const auto country = OptionalLocationField(location, "country");
                    const int weather_code = current.value("weather_code", -1);

                    std::ostringstream text;
                    text << "城市: " << location_name;
                    if (!admin1.empty()) {
                        text << ", " << admin1;
                    }
                    if (!country.empty()) {
                        text << ", " << country;
                    }
                    text << "\n时间: " << current.value("time", "")
                         << "\n天气: " << WeatherCodeToText(weather_code) << " (" << weather_code << ")"
                         << "\n温度: " << current.value("temperature_2m", 0.0) << " "
                         << units.value("temperature_2m", "°C")
                         << "\n湿度: " << current.value("relative_humidity_2m", 0) << " "
                         << units.value("relative_humidity_2m", "%")
                         << "\n降水: " << current.value("precipitation", 0.0) << " "
                         << units.value("precipitation", "mm")
                         << "\n风速: " << current.value("wind_speed_10m", 0.0) << " "
                         << units.value("wind_speed_10m", "km/h")
                         << "\n风向: " << current.value("wind_direction_10m", 0) << " "
                         << units.value("wind_direction_10m", "°")
                         << "\n数据来源: Open-Meteo public API";

                    return MakeTextResult(text.str());
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("获取天气失败: ") + e.what(), true);
                }
            });
        }

        {
            Tool tool;
            tool.name = "write_file";
            tool.description = "write content to a file";
            tool.input_schema.properties = {
                {"path", {{"type", "string"}, {"description", "File path to write to the file"}}},
                {"content", {{"type", "string"}, {"description", "File content to write to the file"}}}
            };
            tool.input_schema.required = {"path", "content"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                ToolResult result;
                std::string path = args["path"].get<std::string>();
                std::string content = args["content"].get<std::string>();
                try {
                    std::ofstream file(path);
                    if (!file.is_open()) {
                        result.is_error = true;
                        result.content.emplace_back(ContentItem{
                            .type = "text",
                            .text = "Error: Failed to open file" + path
                        });
                        return result;
                    }
                    file << content;
                    file.close();
                    result.content.emplace_back(ContentItem{
                        .type = "text",
                        .text = "Successfully written to file" + path
                    });
                } catch (const std::exception& e) {
                    result.is_error = true;
                    result.content.emplace_back(ContentItem{
                        .type = "text",
                        .text =  std::string("Error writing file: ") + e.what()
                    });
                }
                return result;
            });
        }

        {
            Tool tool;
            tool.name = "read_file";
            tool.description = "Read a text file from the current workspace";
            tool.input_schema.properties = {
                {"path", {{"type", "string"}, {"description", "Relative path under the current workspace"}}},
                {"max_chars", {{"type", "integer"}, {"description", "Maximum number of characters to read"}, {"default", 12000}}}
            };
            tool.input_schema.required = {"path"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    if (!args.contains("path") || !args["path"].is_string()) {
                        return MakeTextResult("path 参数必须是字符串", true);
                    }

                    const auto max_chars_value = args.value("max_chars", 12000);
                    if (max_chars_value <= 0) {
                        return MakeTextResult("max_chars 必须大于 0", true);
                    }

                    const auto requested_path = args["path"].get<std::string>();
                    const auto resolved_path = ResolveWorkspacePath(requested_path);
                    if (!std::filesystem::exists(resolved_path)) {
                        return MakeTextResult("文件不存在: " + requested_path, true);
                    }
                    if (!std::filesystem::is_regular_file(resolved_path)) {
                        return MakeTextResult("path 不是普通文件: " + requested_path, true);
                    }

                    bool truncated = false;
                    const auto content = ReadTextFile(
                        resolved_path,
                        static_cast<std::size_t>(max_chars_value),
                        &truncated
                    );

                    std::ostringstream text;
                    text << "Path: " << RelativeDisplayPath(resolved_path)
                         << "\nTruncated: " << (truncated ? "true" : "false")
                         << "\nContent:\n" << content;
                    return MakeTextResult(text.str());
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("读取文件失败: ") + e.what(), true);
                }
            });
        }

        {
            Tool tool;
            tool.name = "list_directory";
            tool.description = "List files and directories under the current workspace";
            tool.input_schema.properties = {
                {"path", {{"type", "string"}, {"description", "Relative directory path under the current workspace"}, {"default", "."}}},
                {"recursive", {{"type", "boolean"}, {"description", "Whether to recurse into subdirectories"}, {"default", false}}},
                {"max_entries", {{"type", "integer"}, {"description", "Maximum number of entries to return"}, {"default", 100}}}
            };

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    const auto requested_path = args.value("path", std::string("."));
                    const bool recursive = args.value("recursive", false);
                    const int max_entries = args.value("max_entries", 100);
                    if (max_entries <= 0) {
                        return MakeTextResult("max_entries 必须大于 0", true);
                    }

                    const auto resolved_path = ResolveWorkspacePath(requested_path);
                    if (!std::filesystem::exists(resolved_path)) {
                        return MakeTextResult("目录不存在: " + requested_path, true);
                    }
                    if (!std::filesystem::is_directory(resolved_path)) {
                        return MakeTextResult("path 不是目录: " + requested_path, true);
                    }

                    std::vector<json> entries;
                    entries.reserve(static_cast<std::size_t>(max_entries));
                    bool truncated = false;

                    auto append_entry = [&](const std::filesystem::directory_entry& entry) {
                        if (entries.size() >= static_cast<std::size_t>(max_entries)) {
                            truncated = true;
                            return;
                        }

                        json item = {
                            {"path", RelativeDisplayPath(entry.path())}
                        };
                        if (entry.is_directory()) {
                            item["type"] = "directory";
                        } else if (entry.is_regular_file()) {
                            item["type"] = "file";
                            item["size_bytes"] = entry.file_size();
                        } else {
                            item["type"] = "other";
                        }
                        entries.emplace_back(std::move(item));
                    };

                    if (recursive) {
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(resolved_path)) {
                            append_entry(entry);
                            if (truncated) {
                                break;
                            }
                        }
                    } else {
                        for (const auto& entry : std::filesystem::directory_iterator(resolved_path)) {
                            append_entry(entry);
                            if (truncated) {
                                break;
                            }
                        }
                    }

                    std::sort(entries.begin(), entries.end(), [](const json& left, const json& right) {
                        return left.at("path").get<std::string>() < right.at("path").get<std::string>();
                    });

                    const json payload = {
                        {"path", requested_path},
                        {"recursive", recursive},
                        {"truncated", truncated},
                        {"entries", entries}
                    };
                    return MakeTextResult(payload.dump(2));
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("列出目录失败: ") + e.what(), true);
                }
            });
        }

        {
            Tool tool;
            tool.name = "read_multiple_files";
            tool.description = "Read multiple text files from the current workspace in one request";
            tool.input_schema.properties = {
                {"paths", {{"type", "array"}, {"description", "Relative file paths under the current workspace"}}},
                {"max_chars_per_file", {{"type", "integer"}, {"description", "Maximum characters to read per file"}, {"default", 4000}}}
            };
            tool.input_schema.required = {"paths"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    if (!args.contains("paths") || !args["paths"].is_array()) {
                        return MakeTextResult("paths 参数必须是字符串数组", true);
                    }

                    const auto max_chars_per_file = args.value("max_chars_per_file", 4000);
                    if (max_chars_per_file <= 0) {
                        return MakeTextResult("max_chars_per_file 必须大于 0", true);
                    }

                    json files = json::array();
                    json errors = json::array();
                    for (const auto& path_value : args["paths"]) {
                        if (!path_value.is_string()) {
                            errors.push_back({
                                {"path", "<non-string>"},
                                {"error", "path item must be a string"}
                            });
                            continue;
                        }

                        const auto requested_path = path_value.get<std::string>();
                        try {
                            const auto resolved_path = ResolveWorkspacePath(requested_path);
                            if (!std::filesystem::exists(resolved_path)) {
                                throw std::runtime_error("file does not exist");
                            }
                            if (!std::filesystem::is_regular_file(resolved_path)) {
                                throw std::runtime_error("path is not a regular file");
                            }
                            files.push_back(BuildReadFileJson(
                                resolved_path,
                                static_cast<std::size_t>(max_chars_per_file)
                            ));
                        } catch (const std::exception& e) {
                            errors.push_back({
                                {"path", requested_path},
                                {"error", e.what()}
                            });
                        }
                    }

                    const json payload = {
                        {"requested_count", args["paths"].size()},
                        {"files", files},
                        {"errors", errors}
                    };
                    return MakeTextResult(payload.dump(2));
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("批量读取文件失败: ") + e.what(), true);
                }
            });
        }

        {
            Tool tool;
            tool.name = "search_workspace";
            tool.description = "Search text files in the current workspace by literal text or regex";
            tool.input_schema.properties = {
                {"query", {{"type", "string"}, {"description", "Search text or regex pattern"}}},
                {"path", {{"type", "string"}, {"description", "Relative directory or file path under the current workspace"}, {"default", "."}}},
                {"mode", {{"type", "string"}, {"description", "Search mode"}, {"enum", json::array({"literal", "regex"})}, {"default", "literal"}}},
                {"case_sensitive", {{"type", "boolean"}, {"description", "Whether the search is case-sensitive"}, {"default", false}}},
                {"max_results", {{"type", "integer"}, {"description", "Maximum number of matching lines to return"}, {"default", 50}}},
                {"max_file_size_bytes", {{"type", "integer"}, {"description", "Skip files larger than this many bytes"}, {"default", 262144}}}
            };
            tool.input_schema.required = {"query"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    if (!args.contains("query") || !args["query"].is_string()) {
                        return MakeTextResult("query 参数必须是字符串", true);
                    }

                    const auto query = args["query"].get<std::string>();
                    if (query.empty()) {
                        return MakeTextResult("query 参数不能为空", true);
                    }

                    const auto requested_path = args.value("path", std::string("."));
                    const auto mode = args.value("mode", std::string("literal"));
                    const bool case_sensitive = args.value("case_sensitive", false);
                    const int max_results = args.value("max_results", 50);
                    const int max_file_size_bytes = args.value("max_file_size_bytes", 262144);
                    if (max_results <= 0) {
                        return MakeTextResult("max_results 必须大于 0", true);
                    }
                    if (max_file_size_bytes <= 0) {
                        return MakeTextResult("max_file_size_bytes 必须大于 0", true);
                    }
                    if (mode != "literal" && mode != "regex") {
                        return MakeTextResult("mode 必须是 literal 或 regex", true);
                    }

                    const auto resolved_path = ResolveWorkspacePath(requested_path);
                    if (!std::filesystem::exists(resolved_path)) {
                        return MakeTextResult("路径不存在: " + requested_path, true);
                    }

                    const bool use_regex = mode == "regex";
                    std::regex pattern;
                    if (use_regex) {
                        const auto flags = case_sensitive
                                               ? std::regex_constants::ECMAScript
                                               : (std::regex_constants::ECMAScript | std::regex_constants::icase);
                        try {
                            pattern = std::regex(query, flags);
                        } catch (const std::regex_error& e) {
                            return MakeTextResult(std::string("regex 无效: ") + e.what(), true);
                        }
                    }
                    const auto lowered_query = case_sensitive ? query : Lowercase(query);

                    int scanned_files = 0;
                    int skipped_large_files = 0;
                    int skipped_binary_files = 0;
                    bool truncated = false;
                    json results = json::array();

                    auto process_file = [&](const std::filesystem::path& file_path) {
                        if (truncated) {
                            return;
                        }
                        if (!std::filesystem::is_regular_file(file_path)) {
                            return;
                        }

                        const auto file_size = std::filesystem::file_size(file_path);
                        if (file_size > static_cast<std::uintmax_t>(max_file_size_bytes)) {
                            ++skipped_large_files;
                            return;
                        }

                        std::string content;
                        bool file_truncated = false;
                        try {
                            content = ReadTextFile(
                                file_path,
                                static_cast<std::size_t>(max_file_size_bytes),
                                &file_truncated
                            );
                        } catch (const std::exception&) {
                            ++skipped_binary_files;
                            return;
                        }

                        ++scanned_files;
                        const auto lines = SplitLines(content);
                        for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
                            const auto& line = lines[line_index];
                            bool matched = false;
                            if (use_regex) {
                                matched = std::regex_search(line, pattern);
                            } else if (case_sensitive) {
                                matched = line.find(query) != std::string::npos;
                            } else {
                                matched = Lowercase(line).find(lowered_query) != std::string::npos;
                            }

                            if (!matched) {
                                continue;
                            }

                            results.push_back({
                                {"path", RelativeDisplayPath(file_path)},
                                {"line_number", static_cast<int>(line_index + 1)},
                                {"line_text", line}
                            });
                            if (results.size() >= static_cast<std::size_t>(max_results)) {
                                truncated = true;
                                return;
                            }
                        }
                    };

                    if (std::filesystem::is_regular_file(resolved_path)) {
                        process_file(resolved_path);
                    } else if (std::filesystem::is_directory(resolved_path)) {
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(resolved_path)) {
                            process_file(entry.path());
                            if (truncated) {
                                break;
                            }
                        }
                    } else {
                        return MakeTextResult("path 既不是普通文件也不是目录: " + requested_path, true);
                    }

                    std::sort(results.begin(), results.end(), [](const json& left, const json& right) {
                        const auto left_path = left.at("path").get<std::string>();
                        const auto right_path = right.at("path").get<std::string>();
                        if (left_path != right_path) {
                            return left_path < right_path;
                        }
                        return left.at("line_number").get<int>() < right.at("line_number").get<int>();
                    });

                    const json payload = {
                        {"query", query},
                        {"path", requested_path},
                        {"mode", mode},
                        {"case_sensitive", case_sensitive},
                        {"truncated", truncated},
                        {"scanned_files", scanned_files},
                        {"skipped_large_files", skipped_large_files},
                        {"skipped_binary_files", skipped_binary_files},
                        {"results", results}
                    };
                    return MakeTextResult(payload.dump(2));
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("搜索工作区失败: ") + e.what(), true);
                }
            });
        }

        {
            Tool tool;
            tool.name = "compare_files";
            tool.description = "Compare two text files in the current workspace and return a structured diff";
            tool.input_schema.properties = {
                {"path_a", {{"type", "string"}, {"description", "First relative file path"}}},
                {"path_b", {{"type", "string"}, {"description", "Second relative file path"}}},
                {"max_chars_per_file", {{"type", "integer"}, {"description", "Maximum characters to read from each file"}, {"default", 20000}}},
                {"max_diff_lines", {{"type", "integer"}, {"description", "Maximum changed lines to return"}, {"default", 200}}}
            };
            tool.input_schema.required = {"path_a", "path_b"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    if (!args.contains("path_a") || !args["path_a"].is_string() ||
                        !args.contains("path_b") || !args["path_b"].is_string()) {
                        return MakeTextResult("path_a 和 path_b 参数都必须是字符串", true);
                    }

                    const auto max_chars_per_file = args.value("max_chars_per_file", 20000);
                    const auto max_diff_lines = args.value("max_diff_lines", 200);
                    if (max_chars_per_file <= 0) {
                        return MakeTextResult("max_chars_per_file 必须大于 0", true);
                    }
                    if (max_diff_lines <= 0) {
                        return MakeTextResult("max_diff_lines 必须大于 0", true);
                    }

                    const auto requested_path_a = args["path_a"].get<std::string>();
                    const auto requested_path_b = args["path_b"].get<std::string>();
                    const auto resolved_path_a = ResolveWorkspacePath(requested_path_a);
                    const auto resolved_path_b = ResolveWorkspacePath(requested_path_b);

                    if (!std::filesystem::exists(resolved_path_a) || !std::filesystem::is_regular_file(resolved_path_a)) {
                        return MakeTextResult("path_a 不是普通文件: " + requested_path_a, true);
                    }
                    if (!std::filesystem::exists(resolved_path_b) || !std::filesystem::is_regular_file(resolved_path_b)) {
                        return MakeTextResult("path_b 不是普通文件: " + requested_path_b, true);
                    }

                    bool truncated_a = false;
                    bool truncated_b = false;
                    const auto content_a = ReadTextFile(
                        resolved_path_a,
                        static_cast<std::size_t>(max_chars_per_file),
                        &truncated_a
                    );
                    const auto content_b = ReadTextFile(
                        resolved_path_b,
                        static_cast<std::size_t>(max_chars_per_file),
                        &truncated_b
                    );

                    const auto lines_a = SplitLines(content_a);
                    const auto lines_b = SplitLines(content_b);
                    const auto operations = BuildLineDiff(lines_a, lines_b);

                    int added_lines = 0;
                    int removed_lines = 0;
                    bool diff_truncated = false;
                    json differences = json::array();
                    for (const auto& operation : operations) {
                        if (operation.type == LineDiffOp::Type::Equal) {
                            continue;
                        }

                        if (differences.size() >= static_cast<std::size_t>(max_diff_lines)) {
                            diff_truncated = true;
                            break;
                        }

                        json item = {
                            {"text", operation.text}
                        };
                        if (operation.type == LineDiffOp::Type::Added) {
                            item["type"] = "added";
                            item["line_b"] = operation.line_b;
                            ++added_lines;
                        } else {
                            item["type"] = "removed";
                            item["line_a"] = operation.line_a;
                            ++removed_lines;
                        }
                        differences.push_back(std::move(item));
                    }

                    const json payload = {
                        {"path_a", RelativeDisplayPath(resolved_path_a)},
                        {"path_b", RelativeDisplayPath(resolved_path_b)},
                        {"equal", content_a == content_b && !truncated_a && !truncated_b},
                        {"truncated_input", truncated_a || truncated_b},
                        {"truncated_diff", diff_truncated},
                        {"summary", {
                            {"line_count_a", lines_a.size()},
                            {"line_count_b", lines_b.size()},
                            {"added_lines", added_lines},
                            {"removed_lines", removed_lines}
                        }},
                        {"differences", differences}
                    };
                    return MakeTextResult(payload.dump(2));
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("比较文件失败: ") + e.what(), true);
                }
            });
        }

        {
            Tool tool;
            tool.name = "read_code_context";
            tool.description = "Read code files with line-number context and file filtering";
            tool.input_schema.properties = {
                {"paths", {{"type", "array"}, {"description", "Relative file paths under the current workspace"}}},
                {"start_line", {{"type", "integer"}, {"description", "1-based inclusive start line"}, {"default", 1}}},
                {"line_count", {{"type", "integer"}, {"description", "Maximum number of lines to return per file"}, {"default", 120}}},
                {"include_line_numbers", {{"type", "boolean"}, {"description", "Whether to include line numbers"}, {"default", true}}},
                {"allowed_suffixes", {{"type", "array"}, {"description", "Optional list of file extensions like .cpp or .py"}}}
            };
            tool.input_schema.required = {"paths"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    if (!args.contains("paths") || !args["paths"].is_array()) {
                        return MakeTextResult("paths 参数必须是字符串数组", true);
                    }

                    const int start_line = args.value("start_line", 1);
                    const int line_count = args.value("line_count", 120);
                    const bool include_line_numbers = args.value("include_line_numbers", true);
                    if (start_line <= 0) {
                        return MakeTextResult("start_line 必须大于 0", true);
                    }
                    if (line_count <= 0) {
                        return MakeTextResult("line_count 必须大于 0", true);
                    }

                    std::vector<std::string> allowed_suffixes;
                    if (args.contains("allowed_suffixes")) {
                        if (!args["allowed_suffixes"].is_array()) {
                            return MakeTextResult("allowed_suffixes 必须是字符串数组", true);
                        }
                        for (const auto& suffix_value : args["allowed_suffixes"]) {
                            if (!suffix_value.is_string()) {
                                return MakeTextResult("allowed_suffixes 必须只包含字符串", true);
                            }
                            allowed_suffixes.push_back(suffix_value.get<std::string>());
                        }
                    }

                    json files = json::array();
                    json errors = json::array();
                    for (const auto& path_value : args["paths"]) {
                        if (!path_value.is_string()) {
                            errors.push_back({
                                {"path", "<non-string>"},
                                {"error", "path item must be a string"}
                            });
                            continue;
                        }

                        const auto requested_path = path_value.get<std::string>();
                        try {
                            const auto resolved_path = ResolveWorkspacePath(requested_path);
                            if (!std::filesystem::exists(resolved_path) || !std::filesystem::is_regular_file(resolved_path)) {
                                throw std::runtime_error("path is not a regular file");
                            }
                            if (!HasAllowedSuffix(resolved_path, allowed_suffixes)) {
                                throw std::runtime_error("file suffix is not allowed");
                            }

                            bool truncated = false;
                            const auto content = ReadTextFile(resolved_path, 50000, &truncated);
                            const auto lines = SplitLines(content);
                            const std::size_t start_index = static_cast<std::size_t>(start_line - 1);
                            if (start_index >= lines.size() && !lines.empty()) {
                                throw std::runtime_error("start_line exceeds file length");
                            }

                            json snippet_lines = json::array();
                            const auto end_index = std::min(lines.size(), start_index + static_cast<std::size_t>(line_count));
                            for (std::size_t index = start_index; index < end_index; ++index) {
                                if (include_line_numbers) {
                                    snippet_lines.push_back({
                                        {"line_number", static_cast<int>(index + 1)},
                                        {"text", lines[index]}
                                    });
                                } else {
                                    snippet_lines.push_back(lines[index]);
                                }
                            }

                            files.push_back({
                                {"path", RelativeDisplayPath(resolved_path)},
                                {"start_line", start_line},
                                {"line_count", static_cast<int>(snippet_lines.size())},
                                {"file_truncated", truncated},
                                {"lines", snippet_lines}
                            });
                        } catch (const std::exception& e) {
                            errors.push_back({
                                {"path", requested_path},
                                {"error", e.what()}
                            });
                        }
                    }

                    const json payload = {
                        {"files", files},
                        {"errors", errors}
                    };
                    return MakeTextResult(payload.dump(2));
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("读取代码上下文失败: ") + e.what(), true);
                }
            });
        }

        {
            Tool tool;
            tool.name = "run_command";
            tool.description = "Run a limited allowlisted command inside the current workspace";
            tool.input_schema.properties = {
                {"command", {{"type", "string"}, {"description", "Allowlisted command name: ls, pwd, cat, head, tail, wc"}}},
                {"args", {{"type", "array"}, {"description", "Command arguments as a string array"}}},
                {"cwd", {{"type", "string"}, {"description", "Relative working directory inside the current workspace"}, {"default", "."}}},
                {"timeout_seconds", {{"type", "integer"}, {"description", "Best-effort timeout in seconds"}, {"default", 5}}},
                {"max_output_chars", {{"type", "integer"}, {"description", "Maximum characters to return from stdout/stderr"}, {"default", 8000}}}
            };
            tool.input_schema.required = {"command"};

            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    if (!args.contains("command") || !args["command"].is_string()) {
                        return MakeTextResult("command 参数必须是字符串", true);
                    }

                    const auto command = args["command"].get<std::string>();
                    const auto cwd = args.value("cwd", std::string("."));
                    const int timeout_seconds = args.value("timeout_seconds", 5);
                    const int max_output_chars = args.value("max_output_chars", 8000);
                    if (timeout_seconds <= 0 || timeout_seconds > 30) {
                        return MakeTextResult("timeout_seconds 必须在 1 到 30 之间", true);
                    }
                    if (max_output_chars <= 0) {
                        return MakeTextResult("max_output_chars 必须大于 0", true);
                    }

                    static const std::map<std::string, bool> allowlisted_commands = {
                        {"pwd", true},
                        {"ls", true},
                        {"cat", true},
                        {"head", true},
                        {"tail", true},
                        {"wc", true}
                    };
                    if (allowlisted_commands.find(command) == allowlisted_commands.end()) {
                        return MakeTextResult("command 不在允许列表中", true);
                    }

                    std::vector<std::string> command_args;
                    if (args.contains("args")) {
                        if (!args["args"].is_array()) {
                            return MakeTextResult("args 参数必须是字符串数组", true);
                        }
                        for (const auto& arg_value : args["args"]) {
                            if (!arg_value.is_string()) {
                                return MakeTextResult("args 必须只包含字符串", true);
                            }
                            const auto argument = arg_value.get<std::string>();
                            if (argument.find('\n') != std::string::npos || argument.find('\r') != std::string::npos) {
                                return MakeTextResult("args 中不能包含换行符", true);
                            }
                            command_args.push_back(argument);
                        }
                    }

                    const auto resolved_cwd = ResolveWorkspacePath(cwd);
                    if (!std::filesystem::exists(resolved_cwd) || !std::filesystem::is_directory(resolved_cwd)) {
                        return MakeTextResult("cwd 不是工作区内的有效目录", true);
                    }

                    const auto workspace_root = std::filesystem::current_path().lexically_normal();
                    std::vector<std::string> normalized_args;
                    int path_arg_count = 0;
                    auto push_resolved_path = [&](const std::string& arg) {
                        const auto resolved_arg_path =
                            ResolvePathWithinRoot(workspace_root, resolved_cwd, arg);
                        normalized_args.push_back(resolved_arg_path.string());
                        ++path_arg_count;
                    };

                    if (command == "pwd") {
                        if (!command_args.empty()) {
                            return MakeTextResult("pwd 不接受参数", true);
                        }
                    } else if (command == "ls") {
                        for (const auto& arg : command_args) {
                            if (!arg.empty() && arg[0] == '-') {
                                if (arg != "-a" && arg != "-l" && arg != "-la" && arg != "-al") {
                                    return MakeTextResult("ls 只允许 -a, -l, -la, -al 参数", true);
                                }
                                normalized_args.push_back(arg);
                            } else {
                                push_resolved_path(arg);
                            }
                        }
                    } else if (command == "cat") {
                        for (const auto& arg : command_args) {
                            if (!arg.empty() && arg[0] == '-') {
                                return MakeTextResult("cat 不允许选项参数", true);
                            }
                            push_resolved_path(arg);
                        }
                        if (path_arg_count == 0) {
                            return MakeTextResult("cat 至少需要一个文件路径参数", true);
                        }
                    } else if (command == "head" || command == "tail") {
                        for (std::size_t index = 0; index < command_args.size(); ++index) {
                            const auto& arg = command_args[index];
                            if (arg == "-n") {
                                if (index + 1 >= command_args.size()) {
                                    return MakeTextResult(command + " 的 -n 参数缺少值", true);
                                }
                                normalized_args.push_back(arg);
                                normalized_args.push_back(command_args[index + 1]);
                                ++index;
                            } else if (!arg.empty() && arg[0] == '-') {
                                return MakeTextResult(command + " 只允许 -n 选项", true);
                            } else {
                                push_resolved_path(arg);
                            }
                        }
                        if (path_arg_count == 0) {
                            return MakeTextResult(command + " 至少需要一个文件路径参数", true);
                        }
                    } else if (command == "wc") {
                        for (const auto& arg : command_args) {
                            if (!arg.empty() && arg[0] == '-') {
                                if (arg != "-l" && arg != "-w" && arg != "-c") {
                                    return MakeTextResult("wc 只允许 -l, -w, -c 选项", true);
                                }
                                normalized_args.push_back(arg);
                            } else {
                                push_resolved_path(arg);
                            }
                        }
                        if (path_arg_count == 0) {
                            return MakeTextResult("wc 至少需要一个文件路径参数", true);
                        }
                    }

                    static const std::map<std::string, std::string> command_binaries = {
                        {"pwd", "/bin/pwd"},
                        {"ls", "/bin/ls"},
                        {"cat", "/bin/cat"},
                        {"head", "/usr/bin/head"},
                        {"tail", "/usr/bin/tail"},
                        {"wc", "/usr/bin/wc"}
                    };
                    const auto executable = command_binaries.at(command);

                    const auto execution = ExecuteAllowlistedCommand(
                        executable,
                        normalized_args,
                        resolved_cwd,
                        timeout_seconds,
                        static_cast<std::size_t>(max_output_chars)
                    );

                    const json payload = {
                        {"command", command},
                        {"args", normalized_args},
                        {"cwd", RelativeDisplayPath(resolved_cwd)},
                        {"exit_code", execution.exit_code},
                        {"timed_out", execution.timed_out},
                        {"output_truncated", execution.output_truncated},
                        {"output", execution.output}
                    };
                    return MakeTextResult(payload.dump(2), execution.exit_code != 0);
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("执行命令失败: ") + e.what(), true);
                }
            });
        }

        {
            auto history_repository =
                std::make_shared<sql::ToolCallHistoryRepository>("data/tool_call_history.sqlite3");
            history_repository->Initialize();

            Tool tool;
            tool.name = "query_tool_history";
            tool.description = "Query recent MCP tool call history from SQLite";
            tool.input_schema.properties = {
                {"tool_name", {{"type", "string"}, {"description", "Optional tool name filter"}, {"default", ""}}},
                {"errors_only", {{"type", "boolean"}, {"description", "Return only failing tool calls"}, {"default", false}}},
                {"limit", {{"type", "integer"}, {"description", "Maximum number of history records"}, {"default", 20}}}
            };

            mcp.RegisterTool(tool, [history_repository](const json& args) -> ToolResult {
                try {
                    const auto tool_name = args.value("tool_name", std::string(""));
                    const bool errors_only = args.value("errors_only", false);
                    const int limit = args.value("limit", 20);
                    if (limit <= 0) {
                        return MakeTextResult("limit 必须大于 0", true);
                    }

                    const auto records = history_repository->FindRecent(tool_name, errors_only, limit);
                    json items = json::array();
                    for (const auto& record : records) {
                        items.push_back({
                            {"id", record.id},
                            {"tool_name", record.tool_name},
                            {"arguments_json", record.arguments_json},
                            {"is_error", record.is_error},
                            {"result_json", record.result_json},
                            {"error_message", record.error_message},
                            {"started_at", record.started_at},
                            {"finished_at", record.finished_at},
                            {"duration_ms", record.duration_ms}
                        });
                    }

                    const json payload = {
                        {"tool_name_filter", tool_name},
                        {"errors_only", errors_only},
                        {"records", items}
                    };
                    return MakeTextResult(payload.dump(2));
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("查询工具历史失败: ") + e.what(), true);
                }
            });
        }

        {
            Tool tool;
            tool.name = "generate_image";
            tool.description = "Generate an image from a prompt and save it to a local file";
            tool.input_schema.properties = {
                {"prompt", {{"type", "string"}, {"description", "Image prompt provided by the client model"}}},
                {"filename", {{"type", "string"},{"description", "Output file name, saved under generated/images"}}},
                {"size", {{"type", "string"}, {"description", "Image size e.g. 1024x1024 or 2K"}, {"enum", json::array({"512x512", "1024x1024", "1024x1792", "1792x1024", "2K", "4K"})}}},
                {"provider", {{"type", "string"}, {"description", "Image provider, defaults to server configuration"}, {"enum", json::array({"doubao", "gemini"})}}},
            };
            tool.input_schema.required = {"prompt", "filename"};
            mcp.RegisterTool(tool, [](const json& args) -> ToolResult {
                try {
                    if (!args.contains("prompt") || !args["prompt"].is_string()) {
                        return MakeTextResult("Error: Missing required argument prompt", true);
                    }
                    if (!args.contains("filename") || !args["filename"].is_string()) {
                        return MakeTextResult("Error: Missing required argument filename", true);
                    }
                    const auto prompt = args["prompt"].get<std::string>();
                    const auto filename = args["filename"].get<std::string>();
                    const auto size = args.value("size", std::string("1024x1024"));
                    if (prompt.empty()) {
                        return MakeTextResult("Error: Missing required argument prompt", true);
                    }
                    if (filename.empty()) {
                        return MakeTextResult("Error: Missing required argument filename", true);
                    }

                    const auto provider = ToLower(args.value(
                        "provider",
                        config::MCP_CONFIG.GetImageGenerationDefaultProvider()
                    ));

                    std::string image_base64;
                    std::string mime_type = "image/png";
                    std::string model;

                    if (provider == "doubao") {
                        const std::string api_key = config::MCP_CONFIG.GetDoubaoImageApiKey();
                        if (api_key.empty()) {
                            return MakeTextResult(
                                "Doubao API key is not configured: set image_generation.doubao.api_key in server.json",
                                true
                            );
                        }

                        model = config::MCP_CONFIG.GetDoubaoImageModel();
                        const std::string api_url = config::MCP_CONFIG.GetDoubaoImageApiUrl();

                        const json request_body = {
                            {"model", model},
                            {"prompt", prompt},
                            {"size", size},
                            {"response_format", "b64_json"},
                            {"stream", false},
                            {"watermark", false}
                        };

                        const auto response = PostJsonToDoubao(api_url, api_key, request_body);
                        std::tie(image_base64, mime_type) = ExtractDoubaoBase64Image(response);
                    } else if (provider == "gemini") {
                        const std::string api_key = config::MCP_CONFIG.GetGeminiImageApiKey();
                        if (api_key.empty()) {
                            return MakeTextResult(
                                "Gemini API key is not configured: set image_generation.gemini.api_key in server.json",
                                true
                            );
                        }

                        model = config::MCP_CONFIG.GetGeminiImageModel();
                        const auto size_config = ResolveImageSizeConfig(size);

                        const json request_body = {
                            {"contents", json::array({
                                {
                                    {"parts", json::array({
                                        {{"text", prompt}}
                                    })}
                                }
                            })},
                            {"generationConfig", {
                                {"responseModalities", json::array({"IMAGE"})},
                                {"imageConfig", {
                                    {"aspectRatio", size_config.aspect_ratio},
                                    {"imageSize", size_config.image_size}
                                }}
                            }}
                        };

                        const auto response = PostJsonToGemini(model, api_key, request_body);
                        std::tie(image_base64, mime_type) = ExtractGeminiInlineImage(response);
                    } else {
                        return MakeTextResult("unsupported image provider: " + provider, true);
                    }

                    const auto image_bytes = DecodeBase64(image_base64);
                    if (image_bytes.empty()) {
                        return MakeTextResult(provider + " returned empty image data", true);
                    }
                    const auto output_path = SaveGeneratedImage(filename, mime_type, image_bytes);

                    return MakeTextResult("Image saved to: " + output_path.string() +
                                          "\nprovider: " + provider +
                                          "\nmodel: " + model);
                } catch (const std::exception& e) {
                    return MakeTextResult(std::string("Generate image failed: ") + e.what(), true);
                }
            });
        }

    }
}
