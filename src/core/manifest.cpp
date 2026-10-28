#include "core/manifest.h"

#include <set>

namespace lz {

bool is_valid_mod_id(const std::string& id) {
    // ids end up as folder names and log tags, keep them boring
    if (id.empty() || id.size() > 64) return false;
    if (id[0] == '.' || id[0] == '-') return false;
    for (char c : id) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    return id.find("..") == std::string::npos;
}

static bool is_plain_dll_name(const std::string& s) {
    // bare file name only. otherwise a mod.ini can say dll=..\..\whatever.dll
    // and we'd load anything on the machine
    if (s.size() < 5) return false;
    if (s.find_first_of("/\\:") != std::string::npos) return false;
    if (s.find("..") != std::string::npos) return false;
    return to_lower(s.substr(s.size() - 4)) == ".dll";
}

bool parse_manifest(const Ini& ini, const std::filesystem::path& dir, ModManifest& out,
                    std::vector<std::string>& errors, std::vector<std::string>& warnings) {
    out = ModManifest{};
    out.dir = dir;
    size_t errs_before = errors.size();

    for (auto& e : ini.errors())
        warnings.push_back("mod.ini line " + std::to_string(e.line) + ": " + e.message);

    if (!ini.has_section("mod")) {
        errors.push_back("missing [mod] section");
        return false;
    }

    out.id = trim(ini.get_or("mod", "id", ""));
    if (!is_valid_mod_id(out.id))
        errors.push_back("invalid or missing id '" + out.id + "' (use lowercase letters, digits, - _ .)");

    out.name = ini.get_or("mod", "name", out.id);
    out.version = ini.get_or("mod", "version", "0.0.0");
    out.author = ini.get_or("mod", "author", "");
    out.description = ini.get_or("mod", "description", "");

    out.dll = ini.get_or("mod", "dll", "");
    if (out.dll.empty())
        errors.push_back("missing dll");
    else if (!is_plain_dll_name(out.dll))
        errors.push_back("dll must be a plain .dll file name inside the mod folder, got '" + out.dll + "'");

    long long api = 0;
    auto api_s = ini.get("mod", "api_version");
    if (!api_s)
        errors.push_back("missing api_version");
    else if (!parse_int(*api_s, api) || api < 1 || api > 1000)
        errors.push_back("api_version must be a positive integer, got '" + *api_s + "'");
    else
        out.api_version = (int)api;

    for (auto& b : split_list(ini.get_or("mod", "game_builds", "any"))) {
        if (to_lower(b) == "any") {
            out.game_builds.clear();
            break;
        }
        out.game_builds.push_back(to_lower(b));
    }

    std::set<std::string> seen;
    for (auto& r : split_list(ini.get_or("mod", "requires", ""))) {
        if (!is_valid_mod_id(r)) {
            errors.push_back("invalid dependency id '" + r + "'");
            continue;
        }
        if (r == out.id) {
            errors.push_back("mod requires itself");
            continue;
        }
        if (seen.insert(r).second) out.requires_.push_back(r);
    }

    static const std::set<std::string> known = {"id", "name", "version", "author", "description",
                                                "dll", "api_version", "game_builds", "requires"};
    for (auto& k : ini.keys("mod"))
        if (!known.count(k)) warnings.push_back("unknown key '" + k + "' in [mod] (typo?)");

    return errors.size() == errs_before;
}

}  // namespace lz
