#include "settings.h"

#include <cerrno>
#include <cstdlib>

namespace gfx {

namespace {

std::string clean(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    s = s.substr(b, e - b + 1);
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

bool to_int(const std::string& s, long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long v = std::strtol(s.c_str(), &end, 10);
    if (errno || *end) return false;
    out = v;
    return true;
}

bool to_double(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    double v = std::strtod(s.c_str(), &end);
    if (errno || *end) return false;
    out = v;
    return true;
}

// reads an int in [lo, hi] or one of the `extra` sentinel values; empty = default
void read_int(const Getter& get, const char* sec, const char* key, int& field, long lo, long hi,
              std::vector<std::string>& w, std::initializer_list<long> extra = {}) {
    auto raw = get(sec, key);
    if (!raw) return;
    auto v = clean(*raw);
    if (v.empty()) return;
    long n = 0;
    bool ok = to_int(v, n);
    bool allowed = ok && n >= lo && n <= hi;
    for (long e : extra) allowed = allowed || (ok && n == e);
    if (!allowed) {
        w.push_back(std::string("[") + sec + "] " + key + " = '" + *raw + "' is out of range (" + std::to_string(lo) +
                    ".." + std::to_string(hi) + "), ignoring it");
        return;
    }
    field = (int)n;
}

}  // namespace

const char* const kDefaultConfig =
    "; Graphics Settings - Project Lazarus\r\n"
    "; The PC version ships with no graphics options at all. This restores the basics.\r\n"
    "; Anything left blank is not touched, so the game behaves exactly as before.\r\n"
    "; Changes take effect the next time the game starts.\r\n"
    "\r\n"
    "[display]\r\n"
    "; fullscreen (exclusive), borderless, maximized, windowed\r\n"
    "window_mode =\r\n"
    "; resolution, e.g. 1920 and 1080. Set both or neither.\r\n"
    "width =\r\n"
    "height =\r\n"
    "; only used in exclusive fullscreen. blank = monitor default\r\n"
    "refresh_rate =\r\n"
    "\r\n"
    "[performance]\r\n"
    "; frame rate cap, e.g. 60, 120, 144. -1 = unlimited. Ignored by Unity while vsync is on.\r\n"
    "target_fps =\r\n"
    "; 0 = off, 1 = on (every frame), 2 = every second frame\r\n"
    "vsync =\r\n"
    "\r\n"
    "[quality]\r\n"
    "; index into the game's quality levels. The log lists the available names on startup.\r\n"
    "; NOTE: The Walking Dead: Destinies ships a single level (0 = Ultra), so this does nothing in this game.\r\n"
    "quality_level =\r\n"
    "; level-of-detail distance multiplier. 1 = default, 2 = models stay detailed twice as far away.\r\n"
    "lod_bias =\r\n"
    "; disable, enable, force\r\n"
    "anisotropic =\r\n"
    "\r\n"
    "[advanced]\r\n"
    "; re-apply after every scene load, in case the game resets something. true/false\r\n"
    "reapply_each_scene = true\r\n";

const char* window_mode_name(int m) {
    switch (m) {
        case kExclusive: return "fullscreen";
        case kBorderless: return "borderless";
        case kMaximized: return "maximized";
        case kWindowed: return "windowed";
    }
    return "unchanged";
}

Settings parse(const Getter& get, std::vector<std::string>& w) {
    Settings s;

    if (auto raw = get("display", "window_mode")) {
        auto v = clean(*raw);
        if (v == "fullscreen" || v == "exclusive") s.window_mode = kExclusive;
        else if (v == "borderless" || v == "fullscreenwindow") s.window_mode = kBorderless;
        else if (v == "maximized" || v == "maximised") s.window_mode = kMaximized;
        else if (v == "windowed" || v == "window") s.window_mode = kWindowed;
        else if (!v.empty()) w.push_back("[display] window_mode = '" + *raw + "' is not one of fullscreen/borderless/maximized/windowed, ignoring it");
    }

    read_int(get, "display", "width", s.width, 640, 16384, w);
    read_int(get, "display", "height", s.height, 360, 16384, w);
    if ((s.width > 0) != (s.height > 0)) {
        w.push_back("[display] width and height must be set together, ignoring resolution");
        s.width = s.height = 0;
    }
    read_int(get, "display", "refresh_rate", s.refresh_rate, 24, 1000, w);

    // below 10fps is never what someone meant, and 0 would freeze unity's frame pacing
    read_int(get, "performance", "target_fps", s.target_fps, 10, 1000, w, {-1});
    read_int(get, "performance", "vsync", s.vsync, 0, 4, w);
    if (s.vsync > 0 && s.target_fps != 0)
        w.push_back("vsync is on, so Unity will ignore target_fps. Set vsync = 0 to use the frame cap.");

    read_int(get, "quality", "quality_level", s.quality_level, 0, 31, w);

    if (auto raw = get("quality", "lod_bias")) {
        auto v = clean(*raw);
        double d = 0;
        if (!v.empty()) {
            if (to_double(v, d) && d >= 0.1 && d <= 10.0) s.lod_bias = d;
            else w.push_back("[quality] lod_bias = '" + *raw + "' must be between 0.1 and 10, ignoring it");
        }
    }

    if (auto raw = get("quality", "anisotropic")) {
        auto v = clean(*raw);
        if (v == "disable" || v == "off") s.anisotropic = 0;
        else if (v == "enable" || v == "on") s.anisotropic = 1;
        else if (v == "force" || v == "forceenable") s.anisotropic = 2;
        else if (!v.empty()) w.push_back("[quality] anisotropic = '" + *raw + "' is not disable/enable/force, ignoring it");
    }

    if (auto raw = get("advanced", "reapply_each_scene")) {
        auto v = clean(*raw);
        if (v == "false" || v == "0" || v == "no" || v == "off") s.reapply_each_scene = false;
        else if (!(v.empty() || v == "true" || v == "1" || v == "yes" || v == "on"))
            w.push_back("[advanced] reapply_each_scene = '" + *raw + "' is not true/false, using true");
    }
    return s;
}

}  // namespace gfx
