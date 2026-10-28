#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "core/ini.h"
#include "core/log.h"

namespace lz {

// Lazarus/lazarus.ini. defaults are chosen so a missing/broken file just means
// "load nothing"
struct LoaderConfig {
    bool enabled = true;          // false = we're just a version.dll forwarder
    bool safe_mode = false;
    LogLevel log_level = LogLevel::Info;
    bool ignore_build_check = false;  // load pinned mods on unknown builds anyway
    std::vector<std::string> enabled_mods;  // order matters, it's the load order
    std::vector<std::string> warnings;
};

// never fails. a typo in lazarus.ini should cost you a warning, not the game
LoaderConfig load_config(const std::filesystem::path& path);
LoaderConfig config_from_ini(const Ini& ini);

// shipped as the default lazarus.ini
extern const char* const kDefaultConfigText;

}  // namespace lz
