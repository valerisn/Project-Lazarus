#include "core/manifest.h"
#include "testing.h"

using namespace lz;

static bool parse(const char* text, ModManifest& m, std::vector<std::string>& errs,
                  std::vector<std::string>& warns) {
    return parse_manifest(Ini::parse(text), "mods/x", m, errs, warns);
}

TEST_CASE("manifest: valid minimal manifest") {
    ModManifest m;
    std::vector<std::string> e, w;
    CHECK(parse("[mod]\nid=graphics-settings\ndll=gfx.dll\napi_version=1\n", m, e, w));
    CHECK(e.empty());
    CHECK_EQ(m.id, "graphics-settings");
    CHECK_EQ(m.name, "graphics-settings");  // falls back to id
    CHECK_EQ(m.api_version, 1);
    CHECK(m.supports_any_build());
    CHECK(m.requires_.empty());
}

TEST_CASE("manifest: full manifest with builds and deps") {
    ModManifest m;
    std::vector<std::string> e, w;
    CHECK(parse("[mod]\nid=a\nname=A Mod\nversion=1.2\ndll=a.dll\napi_version=1\n"
                "game_builds=ABCDEF12, 0011\nrequires=b, c, b\n",
                m, e, w));
    CHECK_EQ(m.game_builds.size(), 2u);
    CHECK_EQ(m.game_builds[0], "abcdef12");  // lowercased
    CHECK_EQ(m.requires_.size(), 2u);        // b only counted once
}

TEST_CASE("manifest: corrupted / missing fields are errors") {
    ModManifest m;
    std::vector<std::string> e, w;
    CHECK(!parse("garbage without sections", m, e, w));
    CHECK(!e.empty());

    e.clear();
    CHECK(!parse("[mod]\nid=Bad Id!\ndll=x.dll\napi_version=1\n", m, e, w));
    e.clear();
    CHECK(!parse("[mod]\nid=ok\napi_version=1\n", m, e, w));  // no dll
    e.clear();
    CHECK(!parse("[mod]\nid=ok\ndll=x.dll\napi_version=one\n", m, e, w));
    e.clear();
    CHECK(!parse("[mod]\nid=ok\ndll=x.dll\n", m, e, w));  // no api_version
    e.clear();
    CHECK(!parse("[mod]\nid=ok\ndll=x.dll\napi_version=1\nrequires=ok\n", m, e, w));
}

TEST_CASE("manifest: dll path traversal rejected") {
    ModManifest m;
    std::vector<std::string> e, w;
    for (auto bad : {"..\\..\\evil.dll", "C:\\Windows\\evil.dll", "sub/evil.dll", "evil.exe", ".dll"}) {
        e.clear();
        std::string text = std::string("[mod]\nid=ok\napi_version=1\ndll=") + bad + "\n";
        CHECK(!parse(text.c_str(), m, e, w));
    }
}

TEST_CASE("manifest: unknown keys warn but don't fail") {
    ModManifest m;
    std::vector<std::string> e, w;
    CHECK(parse("[mod]\nid=ok\ndll=ok.dll\napi_version=1\napi_verison=2\n", m, e, w));
    CHECK_EQ(w.size(), 1u);
}

TEST_CASE("manifest: mod id rules") {
    CHECK(is_valid_mod_id("a"));
    CHECK(is_valid_mod_id("fix.governor-softlock_2"));
    CHECK(!is_valid_mod_id(""));
    CHECK(!is_valid_mod_id("Upper"));
    CHECK(!is_valid_mod_id("-dash"));
    CHECK(!is_valid_mod_id("a..b"));
    CHECK(!is_valid_mod_id(std::string(65, 'a')));
}
