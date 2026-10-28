#include "core/ini.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace lz {

std::string to_lower(std::string_view s) {
    std::string r(s);
    for (auto& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return std::string(s.substr(b, e - b));
}

std::vector<std::string> split_list(std::string_view s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        if (comma == std::string_view::npos) comma = s.size();
        auto item = trim(s.substr(start, comma - start));
        if (!item.empty()) out.push_back(std::move(item));
        start = comma + 1;
    }
    return out;
}

bool parse_bool(std::string_view s, bool& out) {
    auto v = to_lower(trim(s));
    if (v == "1" || v == "true" || v == "yes" || v == "on") { out = true; return true; }
    if (v == "0" || v == "false" || v == "no" || v == "off") { out = false; return true; }
    return false;
}

bool parse_int(std::string_view s, long long& out) {
    auto v = trim(s);
    if (v.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long long r = std::strtoll(v.c_str(), &end, 10);
    if (errno != 0 || end != v.c_str() + v.size()) return false;
    out = r;
    return true;
}

bool parse_float(std::string_view s, double& out) {
    auto v = trim(s);
    if (v.empty()) return false;
    char* end = nullptr;
    errno = 0;
    // strtod is locale dependent. the game seems to run in the C locale, but if
    // anyone ever reports 1.5 turning into 1, it's this
    double r = std::strtod(v.c_str(), &end);
    if (errno != 0 || end != v.c_str() + v.size()) return false;
    out = r;
    return true;
}

Ini Ini::parse(std::string_view text) {
    Ini ini;
    // notepad loves adding a BOM
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text.remove_prefix(3);

    std::string section;
    int lineno = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        auto line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        ++lineno;

        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            auto close = line.find(']');
            if (close == std::string::npos) {
                ini.errors_.push_back({lineno, "unterminated section header"});
                continue;
            }
            section = to_lower(trim(std::string_view(line).substr(1, close - 1)));
            ini.data_[section];  // empty sections should still exist
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            ini.errors_.push_back({lineno, "expected key = value"});
            continue;
        }
        auto key = to_lower(trim(std::string_view(line).substr(0, eq)));
        if (key.empty()) {
            ini.errors_.push_back({lineno, "empty key"});
            continue;
        }
        auto value = trim(std::string_view(line).substr(eq + 1));
        // only treat ; or # as a comment after whitespace, otherwise
        // path=C:\foo;bar gets cut in half
        for (auto marker : {" ;", "\t;", " #", "\t#"}) {
            auto c = value.find(marker);
            if (c != std::string::npos) value = trim(std::string_view(value).substr(0, c));
        }
        auto& sec = ini.data_[section];
        if (sec.count(key))
            ini.errors_.push_back({lineno, "duplicate key '" + key + "', later value wins"});
        sec[key] = value;
    }
    return ini;
}

bool Ini::load(const std::filesystem::path& path, Ini& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open " + path.u8string();
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    out = parse(ss.str());
    return true;
}

std::optional<std::string> Ini::get(std::string_view section, std::string_view key) const {
    auto s = data_.find(to_lower(section));
    if (s == data_.end()) return std::nullopt;
    auto k = s->second.find(to_lower(key));
    if (k == s->second.end()) return std::nullopt;
    return k->second;
}

std::string Ini::get_or(std::string_view section, std::string_view key, std::string_view def) const {
    auto v = get(section, key);
    return v ? *v : std::string(def);
}

bool Ini::has_section(std::string_view section) const { return data_.count(to_lower(section)) != 0; }

std::vector<std::string> Ini::keys(std::string_view section) const {
    std::vector<std::string> out;
    auto s = data_.find(to_lower(section));
    if (s != data_.end())
        for (auto& kv : s->second) out.push_back(kv.first);
    return out;
}

}  // namespace lz
