#include "preset_repo.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace presets;

static void print_usage() {
    std::cout <<
        "Usage:\n"
        "  preset_cli list   <plugin_name> [--category C] [--tag T] [--query Q]\n"
        "  preset_cli show   <preset_id>\n"
        "  preset_cli save   <plugin_name> <preset_name> [--category C] [--tag T]...\n"
        "  preset_cli delete <preset_id>\n"
        "  preset_cli seed\n";
}

static std::string get_env_or_die(const char* key) {
    const char* v = std::getenv(key);
    if (!v) {
        std::cerr << "env " << key << " is not set\n";
        std::exit(1);
    }
    return v;
}

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(); return 1; }

    std::string conn_str = get_env_or_die("PRESET_DB_URL");
    PresetRepo repo(conn_str);

    std::string cmd = argv[1];

    try {
        if (cmd == "seed") {
            auto pid = repo.ensure_plugin("MySynth", "1.0.0", "UniProject");

            Preset p1;
            p1.id        = "11111111-1111-1111-1111-111111111111";
            p1.plugin_id = *pid;
            p1.name      = "Warm Pad";
            p1.category  = "Pad";
            p1.tags      = {"warm", "pad"};
            p1.params    = {{"gain", 0.7}, {"cutoff", int64_t(800)}, {"mode", std::string("lp")}};
            p1.is_factory = true;
            p1.is_public  = true;
            repo.save(p1);

            Preset p2;
            p2.id        = "22222222-2222-2222-2222-222222222222";
            p2.plugin_id = *pid;
            p2.name      = "Sub Bass";
            p2.category  = "Bass";
            p2.tags      = {"sub", "bass"};
            p2.params    = {{"gain", 0.9}, {"cutoff", int64_t(120)}, {"mode", std::string("lp")}};
            p2.is_factory = true;
            p2.is_public  = true;
            repo.save(p2);

            std::cout << "seeded 2 presets for plugin " << *pid << "\n";
        }
        else if (cmd == "list") {
            if (argc < 3) { print_usage(); return 1; }
            auto pid = repo.find_plugin_by_name(argv[2]);
            if (!pid) { std::cerr << "plugin not found: " << argv[2] << "\n"; return 1; }

            std::optional<std::string> cat, tag, q;
            for (int i = 3; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--category" && i+1 < argc) cat = argv[++i];
                else if (a == "--tag" && i+1 < argc) tag = argv[++i];
                else if (a == "--query" && i+1 < argc) q = argv[++i];
            }
            auto list = repo.list_by_plugin(*pid, cat, tag, q);
            for (auto& p : list) {
                std::cout << p.id << "  [" << (p.category ? *p.category : "?")
                          << "]  " << p.name << "  tags=";
                for (size_t i = 0; i < p.tags.size(); ++i)
                    std::cout << (i?",":"") << p.tags[i];
                std::cout << "\n";
            }
            std::cout << "total: " << list.size() << "\n";
        }
        else if (cmd == "show") {
            if (argc < 3) { print_usage(); return 1; }
            auto p = repo.get(argv[2]);
            if (!p) { std::cerr << "preset not found\n"; return 1; }
            std::cout << "id:       " << p->id << "\n"
                      << "name:     " << p->name << "\n"
                      << "category: " << (p->category ? *p->category : "") << "\n"
                      << "tags:     ";
            for (auto& t : p->tags) std::cout << t << " ";
            std::cout << "\nparams:\n";
            for (auto& [k, v] : p->params) {
                std::cout << "  " << k << " = ";
                std::visit([](auto&& x){ std::cout << x; }, v);
                std::cout << "\n";
            }
        }
        else if (cmd == "save") {
            if (argc < 4) { print_usage(); return 1; }
            auto pid = repo.ensure_plugin(argv[2], "1.0.0", "UniProject");
            Preset p;
            p.id        = "33333333-3333-3333-3333-333333333333";
            p.plugin_id = *pid;
            p.name      = argv[3];
            p.params    = {{"gain", 0.5}, {"cutoff", int64_t(1000)}};
            repo.save(p);
            std::cout << "saved preset " << p.id << "\n";
        }
        else if (cmd == "delete") {
            if (argc < 3) { print_usage(); return 1; }
            std::cout << (repo.remove(argv[2]) ? "deleted\n" : "not found\n");
        }
        else {
            print_usage();
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    return 0;
}
