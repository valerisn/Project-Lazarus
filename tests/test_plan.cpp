#include <algorithm>

#include "core/plan.h"
#include "test_util.h"
#include "testing.h"

using namespace lz;

static DiscoveredMod mk(const std::string& id, std::vector<std::string> deps = {},
                        std::vector<std::string> builds = {}, int api = 1) {
    DiscoveredMod d;
    d.folder = id;
    d.valid = true;
    d.manifest.id = id;
    d.manifest.dll = id + ".dll";
    d.manifest.api_version = api;
    d.manifest.requires_ = std::move(deps);
    d.manifest.game_builds = std::move(builds);
    return d;
}

static LoaderConfig cfg_enabled(std::vector<std::string> ids) {
    LoaderConfig c;
    c.enabled_mods = std::move(ids);
    return c;
}

static std::vector<std::string> order(const LoadPlan& p) {
    std::vector<std::string> r;
    for (auto& m : p.load_order) r.push_back(m.id);
    return r;
}

static std::string reason(const LoadPlan& p, const std::string& id) {
    for (auto& s : p.skipped)
        if (s.id == id) return s.reason;
    return "";
}

TEST_CASE("plan: only explicitly enabled mods load") {
    auto p = plan_load({mk("a"), mk("b")}, cfg_enabled({"b"}), {});
    CHECK(order(p) == std::vector<std::string>{"b"});
    CHECK(reason(p, "a").find("not enabled") != std::string::npos);
}

TEST_CASE("plan: deps load first regardless of enabled order") {
    auto p = plan_load({mk("app", {"lib"}), mk("lib")}, cfg_enabled({"app", "lib"}), {});
    CHECK((order(p) == std::vector<std::string>{"lib", "app"}));
}

TEST_CASE("plan: missing and disabled dependencies cascade") {
    auto p = plan_load({mk("a", {"b"}), mk("b", {"c"}), mk("c"), mk("x", {"ghost"})},
                       cfg_enabled({"a", "b", "x"}), {});  // c installed but not enabled
    CHECK(order(p).empty());
    CHECK(reason(p, "b").find("'c' is not being loaded") != std::string::npos);
    CHECK(reason(p, "a").find("'b'") != std::string::npos);
    CHECK(reason(p, "x").find("missing dependency 'ghost'") != std::string::npos);
}

TEST_CASE("plan: dependency cycles are rejected, others still load") {
    auto p = plan_load({mk("a", {"b"}), mk("b", {"a"}), mk("ok")}, cfg_enabled({"a", "b", "ok"}), {});
    CHECK(order(p) == std::vector<std::string>{"ok"});
    CHECK(reason(p, "a") == "circular dependency");
    CHECK(reason(p, "b") == "circular dependency");
}

TEST_CASE("plan: safe mode loads nothing") {
    auto c = cfg_enabled({"a"});
    c.safe_mode = true;
    auto p = plan_load({mk("a")}, c, {});
    CHECK(p.load_order.empty());
    CHECK_EQ(reason(p, "a"), "safe mode is on");
}

TEST_CASE("plan: quarantined mod is skipped") {
    PlanInput in;
    in.quarantined = {"a"};
    auto p = plan_load({mk("a"), mk("b")}, cfg_enabled({"a", "b"}), in);
    CHECK(order(p) == std::vector<std::string>{"b"});
    CHECK(reason(p, "a").find("quarantined") != std::string::npos);
}

TEST_CASE("plan: api version too new is skipped") {
    auto p = plan_load({mk("future", {}, {}, 2)}, cfg_enabled({"future"}), {});
    CHECK(p.load_order.empty());
    CHECK(reason(p, "future").find("update Project Lazarus") != std::string::npos);
}

TEST_CASE("plan: build-pinned mods vs unsupported builds") {
    PlanInput in;
    in.build_fingerprint = "1b579b75b1486c0e";
    auto ok = plan_load({mk("a", {}, {"1b579b75b1486c0e"})}, cfg_enabled({"a"}), in);
    CHECK_EQ(ok.load_order.size(), 1u);

    in.build_fingerprint = "ffffffffffffffff";
    auto bad = plan_load({mk("a", {}, {"1b579b75b1486c0e"})}, cfg_enabled({"a"}), in);
    CHECK(bad.load_order.empty());
    CHECK(reason(bad, "a").find("ffffffffffffffff") != std::string::npos);

    auto c = cfg_enabled({"a"});
    c.ignore_build_check = true;
    auto forced = plan_load({mk("a", {}, {"1b579b75b1486c0e"})}, c, in);
    CHECK_EQ(forced.load_order.size(), 1u);
    CHECK_EQ(forced.warnings.size(), 1u);

    in.build_fingerprint = "";  // couldn't fingerprint at all
    auto unknown = plan_load({mk("a", {}, {"1b579b75b1486c0e"})}, cfg_enabled({"a"}), in);
    CHECK(unknown.load_order.empty());
}

TEST_CASE("plan: invalid manifests and duplicate ids") {
    auto broken = mk("broken");
    broken.valid = false;
    broken.errors = {"missing dll"};
    auto dup = mk("a");
    dup.folder = "a-copy";
    auto p = plan_load({mk("a"), dup, broken, mk("ok")}, cfg_enabled({"a", "broken", "ok"}), {});
    // duplicate id: neither loads, unrelated mods are unaffected
    CHECK(order(p) == std::vector<std::string>{"ok"});
    CHECK(reason(p, "broken").find("missing dll") != std::string::npos);
    CHECK(reason(p, "a").find("more than one installed mod claims this id") != std::string::npos);
}

TEST_CASE("plan: decoy folder can't hijack an enabled id (security review #1)") {
    TempDir td;
    auto mods = td.path / "mods";
    // sorts before the real folder and claims its id
    write_file(mods / "aaa-graphics-pack" / "mod.ini", "[mod]\nid=graphics-settings\ndll=evil.dll\napi_version=1\n");
    write_file(mods / "graphics-settings" / "mod.ini", "[mod]\nid=graphics-settings\ndll=gfx.dll\napi_version=1\n");
    std::vector<std::string> w;
    auto found = discover_mods(mods, w);
    auto p = plan_load(found, cfg_enabled({"graphics-settings"}), {});
    CHECK_EQ(p.load_order.size(), 1u);
    if (p.load_order.size() == 1) CHECK_EQ(p.load_order[0].dll, "gfx.dll");
    bool decoy_rejected = false;
    for (auto& s : p.skipped)
        if (s.reason.find("must match the mod id") != std::string::npos) decoy_rejected = true;
    CHECK(decoy_rejected);
}

TEST_CASE("plan: enabled but not installed warns") {
    auto p = plan_load({}, cfg_enabled({"ghost"}), {});
    CHECK_EQ(p.warnings.size(), 1u);
}

TEST_CASE("discover: scans folders, ignores junk, reports bad manifests") {
    TempDir td;
    auto mods = td.path / "mods";
    write_file(mods / "good" / "mod.ini", "[mod]\nid=good\ndll=good.dll\napi_version=1\n");
    write_file(mods / "bad" / "mod.ini", "[mod]\nid=bad\n");
    write_file(mods / "empty-folder" / "readme.txt", "hi");
    write_file(mods / "Renamed" / "mod.ini", "[mod]\nid=other\ndll=o.dll\napi_version=1\n");
    write_file(mods / "stray.dll", "not a folder");
    std::vector<std::string> w;
    auto found = discover_mods(mods, w);
    CHECK_EQ(found.size(), 3u);
    CHECK_EQ(w.size(), 1u);  // empty-folder
    for (auto& f : found) {
        if (f.folder == "good") CHECK(f.valid);
        if (f.folder == "bad") CHECK(!f.valid && f.errors.size() >= 2);
        if (f.folder == "Renamed") CHECK(!f.valid && f.errors.size() == 1);  // folder must equal id now
    }
}

TEST_CASE("discover: missing mods folder is a warning not a crash") {
    std::vector<std::string> w;
    auto found = discover_mods("no/such/mods", w);
    CHECK(found.empty());
    CHECK_EQ(w.size(), 1u);
}
