#pragma once
// tiny INI reader. INI and not JSON because people edit these in notepad and
// JSON dies on one missing comma

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lz {

struct IniError {
    int line;
    std::string message;
};

class Ini {
public:
    static Ini parse(std::string_view text);
    // false only if the file can't be read. bad lines end up in errors()
    static bool load(const std::filesystem::path& path, Ini& out, std::string& err);

    // case-insensitive
    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    std::string get_or(std::string_view section, std::string_view key, std::string_view def) const;
    bool has_section(std::string_view section) const;
    std::vector<std::string> keys(std::string_view section) const;
    const std::vector<IniError>& errors() const { return errors_; }

private:
    std::map<std::string, std::map<std::string, std::string>> data_;
    std::vector<IniError> errors_;
};

std::string to_lower(std::string_view s);
std::string trim(std::string_view s);
// "a, b ,c" -> {a,b,c}; empty entries dropped
std::vector<std::string> split_list(std::string_view s);

bool parse_bool(std::string_view s, bool& out);
bool parse_int(std::string_view s, long long& out);
bool parse_float(std::string_view s, double& out);

}  // namespace lz
