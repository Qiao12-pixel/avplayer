#pragma once
#include <nlohmann/json.hpp>
namespace avserver {
nlohmann::json QueryWeather(const std::string &city);
nlohmann::json QueryWeatherAt(double latitude, double longitude);
}
