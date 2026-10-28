#include "core/plan.h"

#include <algorithm>
#include <functional>
#include <map>

namespace lz {

std::vector<DiscoveredMod> discover_mods(const std::filesystem::path& mods_dir, std::vector<std::string>& warnings) {
    namespace fs = std::filesystem;
    std::vector<DiscoveredMod> out;
    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) {
        warnings.push_back("mods folder " + mods_dir.u8string() + " does not exist");
        return out;
    }
    std::vector<fs::path> dirs;
    for (auto it = fs::directory_iterator(mods_dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->is_directory(ec)) dirs.push_back(it->path());
    std::sort(dirs.begin(), dirs.end());

    for (auto& d : dirs) {
        auto folder = d.filename().u8string();
        auto ini_path = d / "mod.ini";
        if (!fs::exists(ini_path, ec)) {
            warnings.push_back("folder mods/" + folder + " has no mod.ini, ignoring it");
            continue;
        }
        DiscoveredMod m;
        m.folder = folder;
        Ini ini;
        std::string err;
        if (!Ini::load(ini_path, ini, err)) {
            m.errors.push_back(err);
        } else {
            m.valid = parse_manifest(ini, d, m.manifest, m.errors, m.warnings);
            // used to be a warning. security review: with a mismatch allowed, a
            // folder like "aaa-pack" claiming id = graphics-settings sorted first
            // and ran *its* dll under the id the user enabled. folder == id makes
            // the id unique per mods/ dir, and the user can see what they enabled.
            if (m.valid && m.manifest.id != to_lower(folder)) {
                m.errors.push_back("folder name '" + folder + "' must match the mod id '" + m.manifest.id + "'");
                m.valid = false;
            }
        }
        out.push_back(std::move(m));
    }
    return out;
}

LoadPlan plan_load(const std::vector<DiscoveredMod>& mods, const LoaderConfig& cfg, const PlanInput& in) {
    LoadPlan plan;
    std::map<std::string, const ModManifest*> candidates;
    std::set<std::string> present;
    std::set<std::string> enabled(cfg.enabled_mods.begin(), cfg.enabled_mods.end());

    auto skip = [&](const std::string& id, std::string reason) { plan.skipped.push_back({id, std::move(reason)}); };

    // an id claimed by more than one valid mod loads *none* of them. picking
    // one (we used to take the first by folder order) means the attacker just
    // has to win the sort.
    std::map<std::string, int> id_count;
    for (auto& m : mods)
        if (m.valid) ++id_count[m.manifest.id];

    for (auto& m : mods) {
        if (!m.valid) {
            std::string why = "invalid mod.ini";
            for (auto& e : m.errors) why += "; " + e;
            skip(m.manifest.id.empty() ? m.folder : m.manifest.id, why);
            continue;
        }
        const auto& man = m.manifest;
        present.insert(man.id);
        if (id_count[man.id] > 1) {
            skip(man.id, "more than one installed mod claims this id (folder " + m.folder +
                             ") - none of them will load until the duplicates are removed");
            continue;
        }
        // explicit opt-in is the security model: dropping a DLL into mods/
        // must never be enough on its own to get it executed
        if (!enabled.count(man.id)) {
            skip(man.id, "not enabled (add it to [mods] enabled in lazarus.ini)");
            continue;
        }
        if (cfg.safe_mode) {
            skip(man.id, "safe mode is on");
            continue;
        }
        if (in.quarantined.count(man.id)) {
            skip(man.id, "quarantined after crashing during startup last time (see Lazarus/state/quarantine.txt)");
            continue;
        }
        if (man.api_version > in.loader_api_version) {
            skip(man.id, "needs loader API v" + std::to_string(man.api_version) + ", this loader provides v" +
                             std::to_string(in.loader_api_version) + " - update Project Lazarus");
            continue;
        }
        if (!man.supports_any_build()) {
            bool ok = !in.build_fingerprint.empty() &&
                      std::find(man.game_builds.begin(), man.game_builds.end(), in.build_fingerprint) !=
                          man.game_builds.end();
            if (!ok) {
                if (cfg.ignore_build_check) {
                    plan.warnings.push_back(man.id + " is not marked compatible with game build '" +
                                            in.build_fingerprint + "', loading anyway (ignore_build_check)");
                } else {
                    skip(man.id, "not marked compatible with this game build (" +
                                     (in.build_fingerprint.empty() ? std::string("unknown") : in.build_fingerprint) +
                                     ")");
                    continue;
                }
            }
        }
        candidates[man.id] = &man;
    }

    for (auto& id : cfg.enabled_mods)
        if (!present.count(id)) plan.warnings.push_back("'" + id + "' is enabled in lazarus.ini but no such mod is installed");

    // drop anything whose deps aren't loadable, repeat until nothing changes
    // since dropping one mod can strand another
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = candidates.begin(); it != candidates.end();) {
            std::string missing;
            for (auto& dep : it->second->requires_)
                if (!candidates.count(dep)) {
                    missing = dep;
                    break;
                }
            if (!missing.empty()) {
                skip(it->first, present.count(missing) ? "dependency '" + missing + "' is not being loaded"
                                                       : "missing dependency '" + missing + "'");
                it = candidates.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }

    // topo sort, deps first, otherwise respecting the user's enabled order
    enum Mark { None, Visiting, Done };
    std::map<std::string, Mark> mark;
    std::set<std::string> in_cycle;
    std::function<bool(const std::string&)> visit = [&](const std::string& id) -> bool {
        auto& mk = mark[id];
        if (mk == Done) return true;
        if (mk == Visiting) return false;
        mk = Visiting;
        bool ok = true;
        for (auto& dep : candidates[id]->requires_)
            if (!visit(dep)) ok = false;
        mark[id] = Done;
        if (!ok) {
            in_cycle.insert(id);
            return false;
        }
        plan.load_order.push_back(*candidates[id]);
        return true;
    };
    for (auto& id : cfg.enabled_mods)
        if (candidates.count(id)) visit(id);
    for (auto& id : in_cycle) skip(id, "circular dependency");
    return plan;
}

}  // namespace lz
