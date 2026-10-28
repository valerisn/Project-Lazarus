// #3, the PC version has no rebinding at all.
//
// luckily ApplyBindingOverride(action, newPath, group, path) survived
// stripping. dump the game's input actions to json so people know the real
// names, then apply [bindings]. overrides are in memory only. redone on scene
// load because new scenes can bring their own action assets

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "../dev-console/il2cpp.h"
#include "core/ini.h"
#include "lazarus/lazarus_api.h"
#include "rebind_parse.h"

namespace fs = std::filesystem;

namespace {

const LazarusApi* api = nullptr;
LazarusMod* self = nullptr;

std::vector<rebind::Rebind> g_rebinds;
std::set<std::string> g_dumped;
std::set<size_t> g_reported;       // so we only log each one once

#define LOG(lvl, ...) api->log(self, lvl, __VA_ARGS__)

il2::Klass* asset_class() {
    static il2::Klass* k = nullptr;
    if (!k) k = il2::find_class("UnityEngine.InputSystem.InputActionAsset", "Unity.InputSystem.dll");
    return k;
}

void load_config() {
    g_rebinds.clear();
    lz::Ini ini;
    std::string err;
    auto path = fs::u8path(api->mod_dir(self)) / "config.ini";
    if (!lz::Ini::load(path, ini, err)) return;
    for (auto& key : ini.keys("bindings")) {
        rebind::Rebind r;
        std::string why;
        if (rebind::parse(key, ini.get_or("bindings", key, ""), r, why)) g_rebinds.push_back(r);
        else LOG(LAZARUS_LOG_WARN, "config.ini [bindings] %s: %s", key.c_str(), why.c_str());
    }
}

// one json per asset
int dump_assets(const std::vector<il2::Obj*>& assets, bool force, std::string* where) {
    int written = 0;
    il2::Method* to_json = nullptr;
    for (il2::Obj* a : assets) {
        std::string name = il2::object_name(a);
        if (!force && g_dumped.count(name)) continue;
        if (!to_json) to_json = il2::find_method(il2::class_of(a), "ToJson", 0);
        std::string err;
        std::string json = il2::to_utf8(il2::invoke(to_json, a, nullptr, err));
        if (!err.empty() || json.empty()) {
            LOG(LAZARUS_LOG_WARN, "ToJson on '%s' failed: %s", name.c_str(), err.c_str());
            continue;
        }
        std::string safe = name;
        for (auto& c : safe)
            if (!isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
        auto out = fs::u8path(api->mod_dir(self)) / ("actions-" + safe + ".json");
        FILE* f = _wfopen(out.c_str(), L"wb");
        if (!f) continue;
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
        g_dumped.insert(name);
        if (where) *where = out.parent_path().u8string();
        ++written;
    }
    return written;
}

void apply_all(bool verbose, LazarusPrintFn print = nullptr, void* ctx = nullptr) {
    il2::Klass* k = asset_class();
    if (!k) return;
    std::string err;
    auto assets = il2::find_all(k, err);
    if (assets.empty()) return;

    std::string where;
    if (int n = dump_assets(assets, false, &where))
        LOG(LAZARUS_LOG_INFO, "wrote %d input action list(s) to %s\\actions-*.json", n, where.c_str());

    static il2::Method* apply = nullptr;
    if (!apply) {
        apply = il2::find_method(
            il2::find_class("UnityEngine.InputSystem.InputActionRebindingExtensions", "Unity.InputSystem.dll"),
            "ApplyBindingOverride", 4);
        if (!apply) {
            LOG(LAZARUS_LOG_ERROR, "ApplyBindingOverride(action, newPath, group, path) not found");
            return;
        }
    }
    il2::Method* find_action = il2::find_method(il2::class_of(assets[0]), "FindAction", 2);

    for (size_t i = 0; i < g_rebinds.size(); ++i) {
        auto& r = g_rebinds[i];
        int hits = 0;
        for (il2::Obj* a : assets) {
            bool throw_if_missing = false;
            void* fa_args[] = {il2::new_string(r.action), &throw_if_missing};
            il2::Obj* action = il2::invoke(find_action, a, fa_args, err);
            if (!action) continue;
            void* args[] = {action, il2::new_string(r.to), nullptr, il2::new_string(r.from)};
            std::string aerr;
            il2::invoke(apply, nullptr, args, aerr);
            if (aerr.empty()) ++hits;
            else LOG(LAZARUS_LOG_WARN, "%s: %s", r.action.c_str(), aerr.c_str());
        }
        bool first = !g_reported.count(i);
        if (verbose || first) {
            char line[512];
            if (hits)
                std::snprintf(line, sizeof line, "rebound %s: %s -> %s", r.action.c_str(), r.from.c_str(), r.to.c_str());
            else
                std::snprintf(line, sizeof line, "no action named '%s' in this scene yet (see actions-*.json)",
                              r.action.c_str());
            if (hits || verbose) {
                LOG(hits ? LAZARUS_LOG_INFO : LAZARUS_LOG_WARN, "%s", line);
                if (print) print(ctx, line);
            }
            if (hits) g_reported.insert(i);
        }
    }
}

void on_scene(void*, const char*, int, int mode) {
    if (mode != 1) apply_all(false);  // additive loads don't bring new input assets
}

void cmd_apply(void*, int, const char**, LazarusPrintFn print, void* ctx) {
    load_config();
    g_reported.clear();
    apply_all(true, print, ctx);
    if (g_rebinds.empty()) print(ctx, "no [bindings] in config.ini");
}

void cmd_dump(void*, int, const char**, LazarusPrintFn print, void* ctx) {
    std::string err, where;
    auto assets = il2::find_all(asset_class(), err);
    int n = dump_assets(assets, true, &where);
    std::string msg = std::to_string(n) + " input action list(s) written" + (n ? " to " + where : "");
    print(ctx, msg.c_str());
}

}  // namespace

extern "C" __declspec(dllexport) int lazarus_mod_init(LazarusMod* m, const LazarusApi* a) {
    api = a;
    self = m;
    if (a->api_version < 2 || a->struct_size < sizeof(LazarusApi)) return 1;
    if (!il2::init(a)) {
        a->log(m, LAZARUS_LOG_ERROR, "GameAssembly.dll is missing %s", il2::missing());
        return 2;
    }
    load_config();
    a->register_command(m, "rebind.apply", "reload key-rebind config.ini and apply it now", &cmd_apply, nullptr);
    a->register_command(m, "rebind.dump", "write the game's input actions to actions-*.json", &cmd_dump, nullptr);
    a->log(m, LAZARUS_LOG_INFO, "%zu binding override(s) configured", g_rebinds.size());
    return a->on_scene_loaded(m, &on_scene, nullptr);
}
