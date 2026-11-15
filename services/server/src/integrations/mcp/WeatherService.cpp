#include "WeatherService.h"
#include "network/common/NetworkSupport.h"
#include <cmath>
#include <curl/curl.h>
#include <map>
#include <mutex>
#include <regex>
namespace avserver {
nlohmann::json QueryWeather(const std::string &city) {
  using namespace network;
  if (city.empty() || city.size() > 128 ||
      city.find_first_of("\r\n") != std::string::npos)
    return {{"success", false}, {"error", "请输入有效城市名称"}};
  try {
    auto url = Environment("AVPLAYER_MCP_URL", "http://127.0.0.1:8081/jsonrpc");
    std::smatch parts;
    if (!std::regex_match(url, parts,
                          std::regex(R"(http://([^/:]+)(?::([0-9]+))?(/.*)?)")))
      throw std::runtime_error("天气代理需要HTTP内部MCP地址");
    net::io_context io;
    tcp::resolver resolver(io);
    beast::tcp_stream stream(io);
    net::steady_timer timer(io);
    auto host = parts[1].str(), port = parts[2].matched ? parts[2].str() : "80",
         path = parts[3].matched ? parts[3].str() : "/jsonrpc";
    http::request<http::string_body> request{http::verb::post, path, 11};
    request.set(http::field::host, host);
    request.set(http::field::content_type, "application/json");
    request.body() =
        json{{"jsonrpc", "2.0"},
             {"id", "weather"},
             {"method", "tools/call"},
             {"params",
              {{"name", "get_weather"}, {"arguments", {{"city", city}}}}}}
            .dump();
    request.prepare_payload();
    beast::flat_buffer buffer;
    http::response_parser<http::string_body> parser;
    parser.body_limit(256 * 1024);
    beast::error_code failure;
    bool complete = false;
    auto finish = [&](beast::error_code ec) {
      failure = ec;
      complete = true;
      timer.cancel();
      beast::error_code ignored;
      stream.socket().close(ignored);
    };
    timer.expires_after(std::chrono::seconds(25));
    timer.async_wait([&](beast::error_code ec) {
      if (!ec) {
        failure = net::error::timed_out;
        resolver.cancel();
        beast::error_code ignored;
        stream.socket().close(ignored);
      }
    });
    resolver.async_resolve(
        host, port,
        [&](beast::error_code ec, tcp::resolver::results_type endpoints) {
          if (ec) {
            finish(ec);
            return;
          }
          stream.async_connect(endpoints, [&](beast::error_code ec,
                                              const tcp::endpoint &) {
            if (ec) {
              finish(ec);
              return;
            }
            http::async_write(stream, request,
                              [&](beast::error_code ec, size_t) {
                                if (ec) {
                                  finish(ec);
                                  return;
                                }
                                http::async_read(stream, buffer, parser,
                                                 [&](beast::error_code ec,
                                                     size_t) { finish(ec); });
                              });
          });
        });
    io.run();
    if (!complete || failure)
      throw std::runtime_error("MCP天气查询失败：" + failure.message());
    auto response = parser.release();
    if (response.result() != http::status::ok)
      throw std::runtime_error("MCP HTTP状态异常");
    auto rpc = json::parse(response.body());
    if (rpc.contains("error"))
      throw std::runtime_error(rpc["error"].value("message", "MCP错误"));
    auto result = rpc.at("result");
    std::string text;
    for (const auto &item : result.value("content", json::array()))
      if (item.value("type", "") == "text") {
        if (!text.empty())
          text += '\n';
        text += item.value("text", "");
      }
    if (result.value("isError", false) || text.empty())
      throw std::runtime_error(text.empty() ? "天气工具未返回结果" : text);
    return {{"success", true}, {"city", city}, {"text", text}};
  } catch (const std::exception &error) {
    return {{"success", false}, {"city", city}, {"error", error.what()}};
  }
}

nlohmann::json QueryWeatherAt(double latitude, double longitude) {
  if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 ||
      latitude > 90 || longitude < -180 || longitude > 180)
    return {{"success", false}, {"error", "定位坐标超出有效范围"}};
  try {
    // Only coarse coordinates leave the server. Reverse geocoding is cached and
    // globally serialized, never polled every ten seconds.
    latitude = std::round(latitude * 10) / 10;
    longitude = std::round(longitude * 10) / 10;
    static std::mutex mutex;
    static std::map<std::string, std::string> cities;
    static auto last = std::chrono::steady_clock::time_point{};
    const auto key = std::to_string(latitude) + "," + std::to_string(longitude);
    std::string city;
    {
      std::lock_guard<std::mutex> lock(mutex);
      auto found = cities.find(key);
      if (found != cities.end())
        city = found->second;
      else {
        auto delay = last + std::chrono::milliseconds(1100) -
                     std::chrono::steady_clock::now();
        if (delay > std::chrono::steady_clock::duration::zero())
          std::this_thread::sleep_for(delay);
        static std::once_flag initialized;
        std::call_once(initialized, [] {
          if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("curl init failed");
        });
        auto endpoint =
            Environment("AVPLAYER_GEOCODER_URL",
                        "https://nominatim.openstreetmap.org/reverse");
        auto url =
            endpoint + (endpoint.find('?') == std::string::npos ? "?" : "&") +
            "format=jsonv2&zoom=10&addressdetails=1&accept-language=zh-CN&"
            "lat=" +
            std::to_string(latitude) + "&lon=" + std::to_string(longitude);
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(
            curl_easy_init(), curl_easy_cleanup);
        if (!curl)
          throw std::runtime_error("curl unavailable");
        std::string body;
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(
            curl.get(), CURLOPT_USERAGENT,
            "AVPlayer/0.2 (https://github.com/Qiao12-pixel/avplayer)");
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 3L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 8L);
        curl_easy_setopt(
            curl.get(), CURLOPT_WRITEFUNCTION,
            +[](char *bytes, size_t size, size_t count, void *ctx) -> size_t {
              auto *out = static_cast<std::string *>(ctx);
              size_t length = size * count;
              if (out->size() + length > 256 * 1024)
                return 0;
              out->append(bytes, length);
              return length;
            });
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
        last = std::chrono::steady_clock::now();
        auto status = curl_easy_perform(curl.get());
        long code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
        if (status != CURLE_OK || code != 200)
          throw std::runtime_error("城市反查不可用");
        auto place = nlohmann::json::parse(body);
        auto address = place.value("address", nlohmann::json::object());
        for (const char *field :
             {"city", "town", "municipality", "county", "state"})
          if (address.contains(field) && address[field].is_string()) {
            city = address[field].get<std::string>();
            break;
          }
        const auto province = address.value("state", std::string());
        for (const char *municipality : {"北京", "上海", "天津", "重庆"}) {
          if (province.find(municipality) != std::string::npos) {
            city = std::string(municipality) + "市";
            break;
          }
        }
        if (city.empty())
          throw std::runtime_error("无法识别城市");
        if (cities.size() >= 256)
          cities.clear();
        cities[key] = city;
      }
    }
    return {{"success", true}, {"city", city}};
  } catch (const std::exception &error) {
    return {{"success", false}, {"error", error.what()}};
  }
}
} // namespace avserver
