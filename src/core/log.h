#pragma once
// File logger. One file per session, previous session kept as *.prev.log so
// "it crashed, here's my log" doesn't get clobbered by the relaunch.

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lz {

enum class LogLevel { Trace = 0, Debug, Info, Warn, Error };

const char* level_name(LogLevel l);
bool parse_level(std::string_view s, LogLevel& out);

class Logger {
public:
    // rotates <path> -> <stem>.prev.log, then opens fresh. false if it can't open.
    bool open(const std::filesystem::path& path);
    void close();
    void set_level(LogLevel l) { level_ = l; }
    LogLevel level() const { return level_; }
    bool enabled(LogLevel l) const { return l >= level_; }

    void write(LogLevel l, std::string_view tag, std::string_view msg);
    void writef(LogLevel l, std::string_view tag, const char* fmt, ...);
    void vwritef(LogLevel l, std::string_view tag, const char* fmt, va_list ap);

    // extra sink, used by tests and by the optional debug console
    void set_sink(std::function<void(const std::string&)> sink) { sink_ = std::move(sink); }

    // replace `needle` (case-insensitive) with `replacement` in every line.
    // people paste lazarus.log into public issues, and a game under
    // C:\Users\<name>\ puts their name in it (security review #5).
    // call during startup, before other threads log.
    void add_redaction(const std::string& needle, const std::string& replacement);
    std::string redact(std::string_view msg) const;

    static Logger& global();

private:
    std::mutex mu_;
    FILE* f_ = nullptr;
    LogLevel level_ = LogLevel::Info;
    std::function<void(const std::string&)> sink_;
    std::vector<std::pair<std::string, std::string>> redactions_;  // lowercase needle -> replacement
};

// timestamp + level + tag + message, no newline. split out so tests can poke it.
std::string format_line(LogLevel l, std::string_view tag, std::string_view msg);

}  // namespace lz

#define LZ_LOG(lvl, tag, ...)                                                \
    do {                                                                     \
        auto& lz_lg_ = ::lz::Logger::global();                               \
        if (lz_lg_.enabled(lvl)) lz_lg_.writef(lvl, tag, __VA_ARGS__);       \
    } while (0)
#define LZ_TRACE(tag, ...) LZ_LOG(::lz::LogLevel::Trace, tag, __VA_ARGS__)
#define LZ_DEBUG(tag, ...) LZ_LOG(::lz::LogLevel::Debug, tag, __VA_ARGS__)
#define LZ_INFO(tag, ...) LZ_LOG(::lz::LogLevel::Info, tag, __VA_ARGS__)
#define LZ_WARN(tag, ...) LZ_LOG(::lz::LogLevel::Warn, tag, __VA_ARGS__)
#define LZ_ERROR(tag, ...) LZ_LOG(::lz::LogLevel::Error, tag, __VA_ARGS__)
