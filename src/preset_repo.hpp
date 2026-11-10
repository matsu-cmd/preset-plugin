#pragma once

#include "preset.hpp"
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// forward declaration — не тащим весь pqxx в заголовок
namespace pqxx { class connection; }

namespace presets {

class PresetRepo {
public:
    explicit PresetRepo(const std::string& conn_str);
    ~PresetRepo();

    // non-copyable
    PresetRepo(const PresetRepo&) = delete;
    PresetRepo& operator=(const PresetRepo&) = delete;

    // ---- CRUD ----
    std::optional<Preset> get(const std::string& id);
    void save(const Preset& p);                 // insert or update
    bool remove(const std::string& id);

    // ---- Поиск ----
    std::vector<Preset> list_by_plugin(const std::string& plugin_id,
                                       std::optional<std::string> category = {},
                                       std::optional<std::string> tag      = {},
                                       std::optional<std::string> query    = {});

    // ---- Справочники ----
    std::optional<std::string> find_plugin_by_name(const std::string& name);
    std::optional<std::string> ensure_plugin(const std::string& name,
                                             const std::string& version,
                                             std::optional<std::string> vendor = {});
    std::optional<int> ensure_category(const std::string& name);
    std::optional<std::string> ensure_user(const std::string& username,
                                           const std::string& email);

private:
    std::unique_ptr<pqxx::connection> conn_;
    std::mutex mtx_;  // libpqxx connection не потокобезопасен

    void prepare_statements();
};

} // namespace presets
