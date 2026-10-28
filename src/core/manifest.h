#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "core/ini.h"

namespace lz {

// mods/<id>/mod.ini, looks like:
//
//   [mod]
//   id = graphics-settings
//   name = Graphics Settings
//   version = 1.0.0
//   author = Project Lazarus
//   dll = graphics_settings.dll
//   api_version = 1
//   game_builds = any           ; or comma-separated build fingerprints
//   requires =                  ; other mod ids that must load first
struct ModManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string dll;
    int api_version = 0;
    std::vector<std::string> game_builds;  // empty means "any"
    std::vector<std::string> requires_;    // 'requires' is a keyword in C++20, hence the _
    std::filesystem::path dir;

    bool supports_any_build() const { return game_builds.empty(); }
};

bool is_valid_mod_id(const std::string& id);

// false = unusable, reasons in errors. typos like unknown keys only warn
bool parse_manifest(const Ini& ini, const std::filesystem::path& dir, ModManifest& out,
                    std::vector<std::string>& errors, std::vector<std::string>& warnings);

}  // namespace lz
