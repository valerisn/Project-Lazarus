#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/log.h"
#include "test_util.h"
#include "testing.h"

using namespace lz;

TEST_CASE("log: level parsing") {
    LogLevel l{};
    CHECK(parse_level("Debug", l) && l == LogLevel::Debug);
    CHECK(parse_level(" warning ", l) && l == LogLevel::Warn);
    CHECK(!parse_level("loud", l));
}

TEST_CASE("log: line format has level and tag") {
    auto line = format_line(LogLevel::Warn, "mod:x", "hello");
    CHECK(line.find("[WARN ] [mod:x] hello") != std::string::npos);
}

TEST_CASE("log: filters below level, formats long messages") {
    Logger lg;
    std::vector<std::string> got;
    lg.set_sink([&](const std::string& s) { got.push_back(s); });
    lg.set_level(LogLevel::Info);
    lg.writef(LogLevel::Debug, "t", "dropped %d", 1);
    lg.writef(LogLevel::Info, "t", "kept %d", 2);
    std::string big(5000, 'x');
    lg.writef(LogLevel::Error, "t", "%s", big.c_str());
    CHECK_EQ(got.size(), 2u);
    if (got.size() == 2) {
        CHECK(got[0].find("kept 2") != std::string::npos);
        CHECK(got[1].find(big) != std::string::npos);
    }
}

TEST_CASE("log: user profile path is redacted, case-insensitively") {
    Logger lg;
    std::vector<std::string> got;
    lg.set_sink([&](const std::string& s) { got.push_back(s); });
    lg.add_redaction("C:\\Users\\Keaghan", "%USERPROFILE%");
    lg.add_redaction("C:/Users/Keaghan", "%USERPROFILE%");
    lg.add_redaction("C:", "nope");  // too short, ignored
    lg.write(LogLevel::Info, "t", "game dir: c:\\users\\KEAGHAN\\Games\\TWD and C:/Users/Keaghan/x, C: stays");
    CHECK_EQ(got.size(), 1u);
    if (got.size() == 1) {
        CHECK(got[0].find("%USERPROFILE%\\Games\\TWD") != std::string::npos);
        CHECK(got[0].find("%USERPROFILE%/x") != std::string::npos);
        CHECK(got[0].find("eaghan") == std::string::npos);
        CHECK(got[0].find("C: stays") != std::string::npos);
    }
}

TEST_CASE("log: file rotation keeps previous session") {
    TempDir td;
    auto p = td.path / "logs" / "lazarus.log";
    {
        Logger lg;
        CHECK(lg.open(p));
        lg.write(LogLevel::Info, "t", "first session");
        lg.close();
    }
    {
        Logger lg;
        CHECK(lg.open(p));
        lg.write(LogLevel::Info, "t", "second session");
        lg.close();
    }
    CHECK(read_file(p).find("second session") != std::string::npos);
    CHECK(read_file(td.path / "logs" / "lazarus.prev.log").find("first session") != std::string::npos);
}
