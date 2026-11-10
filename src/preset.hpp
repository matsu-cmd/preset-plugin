#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <variant>
#include <cstdint>
#include <chrono>

namespace presets {

// Значение параметра: может быть числом, целым, булевым или строкой
using ParamValue = std::variant<double, int64_t, bool, std::string>;

struct Preset {
    std::string id;                                     // UUID
    std::string plugin_id;                              // UUID плагина
    std::optional<std::string> author_id;               // UUID автора ()
    std::string name;
    std::optional<std::string> category;                // имя категории ()
    std::vector<std::string> tags;
    std::unordered_map<std::string, ParamValue> params;
    bool is_factory = false;
    bool is_public  = false;
    std::chrono::system_clock::time_point updated_at{};
};

} // namespace presets
