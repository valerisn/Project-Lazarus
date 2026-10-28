#include "core/command.h"
#include "testing.h"

using namespace lz;

TEST_CASE("command: tokenize basics and quotes") {
    auto t = tokenize("  load   \"23 Too Far\"  now ");
    CHECK_EQ(t.size(), 3u);
    if (t.size() == 3) {
        CHECK_EQ(t[0], "load");
        CHECK_EQ(t[1], "23 Too Far");
        CHECK_EQ(t[2], "now");
    }
    CHECK(tokenize("").empty());
    CHECK(tokenize("   \t ").empty());
    auto e = tokenize("say \"\" x");
    CHECK_EQ(e.size(), 3u);
    auto esc = tokenize("say \"a \\\"b\\\" c\"");
    CHECK(esc.size() == 2 && esc[1] == "a \"b\" c");
    auto open = tokenize("say \"unterminated words");
    CHECK(open.size() == 2 && open[1] == "unterminated words");
}

TEST_CASE("command: registry executes, case-insensitive, passes args") {
    CommandRegistry r;
    std::vector<std::string> got;
    CHECK(r.add("Echo", "repeat", "a", [&](const std::vector<std::string>& a, const CommandRegistry::Print& p) {
        got = a;
        p("ok");
    }));
    std::string printed;
    auto print = [&](const std::string& s) { printed += s; };
    CHECK(r.execute("ECHO one \"two three\"", print) == CommandRegistry::Result::Ok);
    CHECK(got.size() == 3 && got[2] == "two three");
    CHECK_EQ(printed, "ok");
    CHECK(r.execute("   ", print) == CommandRegistry::Result::Empty);
}

TEST_CASE("command: unknown command suggests near matches") {
    CommandRegistry r;
    r.add("godmode", "", "a", [](auto&, auto&) {});
    r.add("timescale", "", "a", [](auto&, auto&) {});
    std::string printed;
    CHECK(r.execute("god", [&](const std::string& s) { printed = s; }) == CommandRegistry::Result::NotFound);
    CHECK(printed.find("godmode") != std::string::npos);
    CHECK(printed.find("timescale") == std::string::npos);
}

TEST_CASE("command: ownership and invalid names") {
    CommandRegistry r;
    auto noop = [](auto&, auto&) {};
    CHECK(r.add("load", "", "mod-a", noop));
    CHECK(!r.add("LOAD", "", "mod-b", noop));  // someone else's
    CHECK(r.add("load", "new help", "mod-a", noop));  // same owner can replace it
    CHECK(!r.add("bad name", "", "mod-a", noop));
    CHECK(!r.add("", "", "mod-a", noop));
    r.add("other", "", "mod-b", noop);
    r.remove_owner("mod-a");
    CHECK_EQ(r.entries().size(), 1u);
}
