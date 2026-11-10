#include "preset_repo.hpp"

#include <pqxx/pqxx>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <sstream>

using json = nlohmann::json;

namespace presets {

// ---------------------------------------------------------------
// helpers: ParamValue <-> JSON
// ---------------------------------------------------------------
static ParamValue json_to_param(const json& j) {
    if (j.is_boolean())            return j.get<bool>();
    if (j.is_number_integer())     return j.get<int64_t>();
    if (j.is_number_float())       return j.get<double>();
    if (j.is_string())             return j.get<std::string>();
    throw std::runtime_error("unsupported param type in JSON");
}

static json param_to_json(const ParamValue& v) {
    return std::visit([](auto&& x) { return json(x); }, v);
}

static json params_to_json(const std::unordered_map<std::string, ParamValue>& p) {
    json out = json::object();
    for (const auto& [k, v] : p) out[k] = param_to_json(v);
    return out;
}

static std::unordered_map<std::string, ParamValue>
json_to_params(const std::string& s) {
    std::unordered_map<std::string, ParamValue> out;
    auto j = json::parse(s);
    for (auto& [k, v] : j.items()) out[k] = json_to_param(v);
    return out;
}

// text[] из libpqxx в vector<string>
static std::vector<std::string> parse_pg_array(const std::string& s) {
    std::vector<std::string> out;
    if (s.size() < 2 || s.front() != '{' || s.back() != '}') return out;
    std::string inner = s.substr(1, s.size() - 2);
    if (inner.empty()) return out;

    std::string cur;
    bool in_quotes = false;
    for (size_t i = 0; i < inner.size(); ++i) {
        char c = inner[i];
        if (c == '"' && (i == 0 || inner[i-1] != '\\')) {
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

static std::string to_pg_array(const std::vector<std::string>& v) {
    std::string s = "{";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ',';
        s += '"';
        for (char c : v[i]) {
            if (c == '"' || c == '\\') s += '\\';
            s += c;
        }
        s += '"';
    }
    s += '}';
    return s;
}

// ---------------------------------------------------------------
// PresetRepo
// ---------------------------------------------------------------
PresetRepo::PresetRepo(const std::string& conn_str)
    : conn_(std::make_unique<pqxx::connection>(conn_str))
{
    if (!conn_->is_open())
        throw std::runtime_error("failed to open PostgreSQL connection");
    prepare_statements();
}

PresetRepo::~PresetRepo() = default;

void PresetRepo::prepare_statements() {
    // -------- get --------
    conn_->prepare("preset_get",
        "SELECT p.id::text, p.plugin_id::text, p.author_id::text, p.name, "
        "       c.name AS category, "
        "       COALESCE(array_to_string(ARRAY("
        "         SELECT t.name FROM preset_tags pt JOIN tags t ON t.id=pt.tag_id "
        "         WHERE pt.preset_id=p.id ORDER BY t.name), ','), '') AS tags_csv, "
        "       p.params::text, p.is_factory, p.is_public, p.updated_at "
        "FROM presets p "
        "LEFT JOIN categories c ON c.id = p.category_id "
        "WHERE p.id = $1::uuid");

    // -------- save (upsert) --------
    conn_->prepare("preset_save",
        "INSERT INTO presets (id, plugin_id, author_id, category_id, name, params, "
        "                     is_factory, is_public) "
        "VALUES ($1::uuid, $2::uuid, $3::uuid, $4::int, $5, $6::jsonb, $7, $8) "
        "ON CONFLICT (id) DO UPDATE SET "
        "  plugin_id  = EXCLUDED.plugin_id, "
        "  author_id  = EXCLUDED.author_id, "
        "  category_id= EXCLUDED.category_id, "
        "  name       = EXCLUDED.name, "
        "  params     = EXCLUDED.params, "
        "  is_factory = EXCLUDED.is_factory, "
        "  is_public  = EXCLUDED.is_public");

    // -------- remove --------
    conn_->prepare("preset_remove",
        "DELETE FROM presets WHERE id = $1::uuid");

    // -------- list_by_plugin --------
    conn_->prepare("preset_list",
        "SELECT p.id::text, p.plugin_id::text, p.author_id::text, p.name, "
        "       c.name AS category, "
        "       COALESCE(array_to_string(ARRAY("
        "         SELECT t.name FROM preset_tags pt JOIN tags t ON t.id=pt.tag_id "
        "         WHERE pt.preset_id=p.id ORDER BY t.name), ','), '') AS tags_csv, "
        "       p.params::text, p.is_factory, p.is_public, p.updated_at "
        "FROM presets p "
        "LEFT JOIN categories c ON c.id = p.category_id "
        "WHERE p.plugin_id = $1::uuid "
        "  AND ($2::text IS NULL OR c.name = $2) "
        "  AND ($3::text IS NULL OR EXISTS ("
        "        SELECT 1 FROM preset_tags pt JOIN tags t ON t.id=pt.tag_id "
        "        WHERE pt.preset_id = p.id AND t.name = $3)) "
        "  AND ($4::text IS NULL OR p.name ILIKE '%'||$4||'%') "
        "ORDER BY c.name NULLS LAST, p.name");

    // -------- справочники --------
    conn_->prepare("plugin_find",
        "SELECT id::text FROM plugins WHERE name = $1");

    conn_->prepare("plugin_upsert",
        "INSERT INTO plugins (name, version, vendor) VALUES ($1,$2,$3) "
        "ON CONFLICT (name) DO UPDATE SET version = EXCLUDED.version "
        "RETURNING id::text");

    conn_->prepare("category_upsert",
        "INSERT INTO categories (name) VALUES ($1) "
        "ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name "
        "RETURNING id");

    conn_->prepare("user_upsert",
        "INSERT INTO users (username, email) VALUES ($1,$2) "
        "ON CONFLICT (username) DO UPDATE SET email = EXCLUDED.email "
        "RETURNING id::text");

    // -------- теги --------
    conn_->prepare("tag_upsert",
        "INSERT INTO tags (name) VALUES ($1) "
        "ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name "
        "RETURNING id");

    conn_->prepare("preset_tag_add",
        "INSERT INTO preset_tags (preset_id, tag_id) VALUES ($1::uuid, $2) "
        "ON CONFLICT DO NOTHING");

    conn_->prepare("preset_tag_clear",
        "DELETE FROM preset_tags WHERE preset_id = $1::uuid");
}

// ---------------------------------------------------------------
std::optional<Preset> PresetRepo::get(const std::string& id) {
    std::lock_guard lk(mtx_);
    pqxx::nontransaction tx{*conn_};
    auto r = tx.exec_prepared("preset_get", id);
    if (r.empty()) return std::nullopt;

    auto row = r[0];
    Preset p;
    p.id        = row["id"].as<std::string>();
    p.plugin_id = row["plugin_id"].as<std::string>();
    if (!row["author_id"].is_null())
        p.author_id = row["author_id"].as<std::string>();
    p.name = row["name"].as<std::string>();
    if (!row["category"].is_null())
        p.category = row["category"].as<std::string>();

    auto tags_csv = row["tags_csv"].as<std::string>();
    if (!tags_csv.empty()) {
        std::stringstream ss(tags_csv);
        std::string t;
        while (std::getline(ss, t, ',')) if (!t.empty()) p.tags.push_back(t);
    }

    p.params     = json_to_params(row["params"].as<std::string>());
    p.is_factory = row["is_factory"].as<bool>();
    p.is_public  = row["is_public"].as<bool>();
    return p;
}

// ---------------------------------------------------------------
void PresetRepo::save(const Preset& p) {
    std::lock_guard lk(mtx_);
    pqxx::work tx{*conn_};

    // category_id
    std::optional<int> cat_id;
    if (p.category && !p.category->empty()) {
        auto r = tx.exec_prepared("category_upsert", *p.category);
        cat_id = r[0][0].as<int>();
    }

    // author_id
    std::optional<std::string> author = p.author_id;

    // upsert preset
    auto r = tx.exec_prepared("preset_save",
        p.id,
        p.plugin_id,
        author,                                    // nullopt -> NULL
        cat_id,                                    // nullopt -> NULL
        p.name,
        params_to_json(p.params).dump(),
        p.is_factory,
        p.is_public);

    // теги — сбрасываем и вставляем заново
    tx.exec_prepared("preset_tag_clear", p.id);
    for (const auto& tag : p.tags) {
        auto tr = tx.exec_prepared("tag_upsert", tag);
        int tag_id = tr[0][0].as<int>();
        tx.exec_prepared("preset_tag_add", p.id, tag_id);
    }

    tx.commit();
    (void)r;
}

// ---------------------------------------------------------------
bool PresetRepo::remove(const std::string& id) {
    std::lock_guard lk(mtx_);
    pqxx::work tx{*conn_};
    auto r = tx.exec_prepared("preset_remove", id);
    tx.commit();
    return r.affected_rows() > 0;
}

// ---------------------------------------------------------------
std::vector<Preset> PresetRepo::list_by_plugin(
    const std::string& plugin_id,
    std::optional<std::string> category,
    std::optional<std::string> tag,
    std::optional<std::string> query)
{
    std::lock_guard lk(mtx_);
    pqxx::nontransaction tx{*conn_};
    auto r = tx.exec_prepared("preset_list",
        plugin_id, category, tag, query);

    std::vector<Preset> out;
    for (auto row : r) {
        Preset p;
        p.id        = row["id"].as<std::string>();
        p.plugin_id = row["plugin_id"].as<std::string>();
        if (!row["author_id"].is_null())
            p.author_id = row["author_id"].as<std::string>();
        p.name = row["name"].as<std::string>();
        if (!row["category"].is_null())
            p.category = row["category"].as<std::string>();

        auto tags_csv = row["tags_csv"].as<std::string>();
        if (!tags_csv.empty()) {
            std::stringstream ss(tags_csv);
            std::string t;
            while (std::getline(ss, t, ',')) if (!t.empty()) p.tags.push_back(t);
        }

        p.params     = json_to_params(row["params"].as<std::string>());
        p.is_factory = row["is_factory"].as<bool>();
        p.is_public  = row["is_public"].as<bool>();
        out.push_back(std::move(p));
    }
    return out;
}

// ---------------------------------------------------------------
std::optional<std::string> PresetRepo::find_plugin_by_name(const std::string& name) {
    std::lock_guard lk(mtx_);
    pqxx::nontransaction tx{*conn_};
    auto r = tx.exec_prepared("plugin_find", name);
    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

std::optional<std::string> PresetRepo::ensure_plugin(
    const std::string& name, const std::string& version,
    std::optional<std::string> vendor)
{
    std::lock_guard lk(mtx_);
    pqxx::work tx{*conn_};
    auto r = tx.exec_prepared("plugin_upsert", name, version, vendor);
    tx.commit();
    return r[0][0].as<std::string>();
}

std::optional<int> PresetRepo::ensure_category(const std::string& name) {
    std::lock_guard lk(mtx_);
    pqxx::work tx{*conn_};
    auto r = tx.exec_prepared("category_upsert", name);
    tx.commit();
    return r[0][0].as<int>();
}

std::optional<std::string> PresetRepo::ensure_user(
    const std::string& username, const std::string& email)
{
    std::lock_guard lk(mtx_);
    pqxx::work tx{*conn_};
    auto r = tx.exec_prepared("user_upsert", username, email);
    tx.commit();
    return r[0][0].as<std::string>();
}

} // namespace presets
