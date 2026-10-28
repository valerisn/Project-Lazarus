#include "core/state.h"
#include "test_util.h"
#include "testing.h"

using namespace lz;

TEST_CASE("bootguard: clean shutdown leaves no stale lock") {
    TempDir td;
    BootGuard g(td.path / "state");
    g.begin("mod:a");
    g.end();
    CHECK_EQ(g.take_stale_lock(), "");
}

TEST_CASE("bootguard: crash mid-init is detected once") {
    TempDir td;
    {
        BootGuard g(td.path / "state");
        g.begin("mod:a");
        // "crash": never call end()
    }
    BootGuard g2(td.path / "state");
    CHECK_EQ(g2.take_stale_lock(), "mod:a");
    CHECK_EQ(g2.take_stale_lock(), "");  // consumed
}

TEST_CASE("bootguard: empty lock file still counts as a crash") {
    TempDir td;
    write_file(td.path / "state" / "boot.lock", "");
    BootGuard g(td.path / "state");
    CHECK_EQ(g.take_stale_lock(), "<unknown>");
}

TEST_CASE("bootguard: quarantine round trip, user can edit file") {
    TempDir td;
    BootGuard g(td.path / "state");
    CHECK(g.load_quarantine().empty());
    g.quarantine("bad-mod", "crashed during init");
    g.quarantine("other", "x");
    auto q = g.load_quarantine();
    CHECK(q.count("bad-mod") && q.count("other") && q.size() == 2);

    write_file(g.quarantine_path(), "; comment\n\nOTHER | whenever | reason\n");
    q = g.load_quarantine();
    CHECK(q.size() == 1 && q.count("other"));
}

TEST_CASE("fingerprint: sha256 of known data") {
    TempDir td;
    write_file(td.path / "abc.bin", "abc");
    CHECK_EQ(sha256_hex(td.path / "abc.bin"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_EQ(sha256_hex(td.path / "missing.bin"), "");
}

TEST_CASE("fingerprint: cached by size+mtime and refreshed on change") {
    TempDir td;
    auto ga = td.path / "GameAssembly.dll";
    auto cache = td.path / "state" / "fingerprint.cache";
    write_file(ga, "abc");
    auto fp = build_fingerprint(ga, cache);
    CHECK_EQ(fp, "ba7816bf8f01cfea");
    CHECK(read_file(cache).find("ba7816bf8f01cfea") != std::string::npos);
    CHECK_EQ(build_fingerprint(ga, cache), fp);

    write_file(ga, "abcd");  // different size -> cache miss
    CHECK(build_fingerprint(ga, cache) != fp);
    CHECK_EQ(build_fingerprint(td.path / "nope.dll", cache), "");
}

TEST_CASE("fingerprint: known build table") {
    CHECK(find_known_build("1b579b75b1486c0e") != nullptr);
    CHECK(find_known_build("0000000000000000") == nullptr);
}
