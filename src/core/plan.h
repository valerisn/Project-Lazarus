#pragma once
// works out what actually loads, in what order, and why everything else
// didn't. the skip reasons go straight into the log, so write them for
// players not for us

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/manifest.h"

namespace lz {

struct DiscoveredMod {
    std::string folder;  // for messages when the id itself is broken
    ModManifest manifest;
    bool valid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// sorted by folder name, directory_iterator order isn't guaranteed and I don't
// want load order changing between machines
std::vector<DiscoveredMod> discover_mods(const std::filesystem::path& mods_dir, std::vector<std::string>& warnings);

struct SkippedMod {
    std::string id;
    std::string reason;
};

struct LoadPlan {
    std::vector<ModManifest> load_order;
    std::vector<SkippedMod> skipped;
    std::vector<std::string> warnings;
};

struct PlanInput {
    int loader_api_version = 1;
    std::string build_fingerprint;  // "" if we couldn't hash GameAssembly
    std::set<std::string> quarantined;
};

LoadPlan plan_load(const std::vector<DiscoveredMod>& mods, const LoaderConfig& cfg, const PlanInput& in);

}  // namespace lz
