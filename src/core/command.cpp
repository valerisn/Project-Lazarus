#include "core/command.h"

#include "core/ini.h"

namespace lz {

std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false, have = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '\\' && i + 1 < line.size() && line[i + 1] == '"') {
                cur += '"';
                ++i;
            } else if (c == '"') {
                in_quotes = false;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            in_quotes = true;
            have = true;  // so "" still counts as an argument
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (have) {
                out.push_back(cur);
                cur.clear();
                have = false;
            }
        } else {
            cur += c;
            have = true;
        }
    }
    // unterminated quote - just take it, erroring here would only annoy people
    if (have) out.push_back(cur);
    return out;
}

bool is_valid_command_name(const std::string& name) {
    if (name.empty() || name.size() > 48) return false;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                  c == '-';
        if (!ok) return false;
    }
    return true;
}

bool CommandRegistry::add(const std::string& name, const std::string& help, const std::string& owner, Handler fn) {
    if (!is_valid_command_name(name) || !fn) return false;
    auto key = to_lower(name);
    auto it = cmds_.find(key);
    if (it != cmds_.end() && it->second.owner != owner) return false;
    cmds_[key] = {help, owner, std::move(fn)};
    return true;
}

void CommandRegistry::remove_owner(const std::string& owner) {
    for (auto it = cmds_.begin(); it != cmds_.end();)
        it = (it->second.owner == owner) ? cmds_.erase(it) : std::next(it);
}

CommandRegistry::Result CommandRegistry::execute(std::string_view line, const Print& print) const {
    auto args = tokenize(line);
    if (args.empty()) return Result::Empty;
    auto key = to_lower(args[0]);
    auto it = cmds_.find(key);
    if (it == cmds_.end()) {
        std::string near;
        for (auto& kv : cmds_)
            if (kv.first.find(key) != std::string::npos || key.find(kv.first) != std::string::npos)
                near += (near.empty() ? "" : ", ") + kv.first;
        print("unknown command '" + args[0] + "'" + (near.empty() ? ", try 'help'" : ". did you mean: " + near));
        return Result::NotFound;
    }
    it->second.fn(args, print);
    return Result::Ok;
}

}  // namespace lz
