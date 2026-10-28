#include "core/log.h"

#include <chrono>
#include <ctime>
#include <system_error>
#include <vector>

#include "core/ini.h"

namespace lz {

const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

bool parse_level(std::string_view s, LogLevel& out) {
    auto v = to_lower(trim(s));
    if (v == "trace") out = LogLevel::Trace;
    else if (v == "debug") out = LogLevel::Debug;
    else if (v == "info") out = LogLevel::Info;
    else if (v == "warn" || v == "warning") out = LogLevel::Warn;
    else if (v == "error") out = LogLevel::Error;
    else return false;
    return true;
}

std::string format_line(LogLevel l, std::string_view tag, std::string_view msg) {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    auto ms = (int)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
    localtime_s(&tm, &t);
    char ts[32];
    std::snprintf(ts, sizeof ts, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    std::string out;
    out.reserve(48 + tag.size() + msg.size());
    out += ts;
    out += " [";
    out += level_name(l);
    out += "] [";
    out += tag;
    out += "] ";
    out += msg;
    return out;
}

bool Logger::open(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> g(mu_);
    if (f_) {
        std::fclose(f_);
        f_ = nullptr;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (std::filesystem::exists(path, ec)) {
        auto prev = path.parent_path() / (path.stem().wstring() + L".prev" + path.extension().wstring());
        std::filesystem::remove(prev, ec);
        std::filesystem::rename(path, prev, ec);  // if this fails we just overwrite, not worth dying over
    }
    // _SH_DENYWR: let people open the log in an editor while the game runs
    f_ = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
    return f_ != nullptr;
}

void Logger::close() {
    std::lock_guard<std::mutex> g(mu_);
    if (f_) {
        std::fclose(f_);
        f_ = nullptr;
    }
}

void Logger::add_redaction(const std::string& needle, const std::string& replacement) {
    // too-short needles would shred normal text ("C:" etc), not worth it
    if (needle.size() < 4) return;
    redactions_.push_back({to_lower(needle), replacement});
}

std::string Logger::redact(std::string_view msg) const {
    std::string out(msg);
    for (auto& r : redactions_) {
        // lowercase copy just for searching, positions line up 1:1 for ascii
        // and profile paths are what we care about here
        std::string low = to_lower(out);
        size_t pos = 0;
        std::string rebuilt;
        size_t last = 0;
        while ((pos = low.find(r.first, pos)) != std::string::npos) {
            rebuilt.append(out, last, pos - last);
            rebuilt += r.second;
            pos += r.first.size();
            last = pos;
        }
        if (last) {
            rebuilt.append(out, last, std::string::npos);
            out.swap(rebuilt);
        }
    }
    return out;
}

void Logger::write(LogLevel l, std::string_view tag, std::string_view msg) {
    if (!enabled(l)) return;
    auto line = redactions_.empty() ? format_line(l, tag, msg) : format_line(l, tag, redact(msg));
    std::lock_guard<std::mutex> g(mu_);
    if (f_) {
        std::fwrite(line.data(), 1, line.size(), f_);
        std::fwrite("\r\n", 1, 2, f_);
        // flush every line. it's slower, but the whole point of this log is to
        // survive the game crashing right after we write it
        std::fflush(f_);
    }
    if (sink_) sink_(line);
}

void Logger::vwritef(LogLevel l, std::string_view tag, const char* fmt, va_list ap) {
    if (!enabled(l)) return;
    char stackbuf[1024];
    va_list ap2;
    va_copy(ap2, ap);
    int n = std::vsnprintf(stackbuf, sizeof stackbuf, fmt, ap);
    if (n < 0) {
        va_end(ap2);
        write(l, tag, "<log format error>");
        return;
    }
    if ((size_t)n < sizeof stackbuf) {
        va_end(ap2);
        write(l, tag, std::string_view(stackbuf, (size_t)n));
        return;
    }
    std::vector<char> big((size_t)n + 1);
    std::vsnprintf(big.data(), big.size(), fmt, ap2);
    va_end(ap2);
    write(l, tag, std::string_view(big.data(), (size_t)n));
}

void Logger::writef(LogLevel l, std::string_view tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwritef(l, tag, fmt, ap);
    va_end(ap);
}

Logger& Logger::global() {
    static Logger g;
    return g;
}

}  // namespace lz
