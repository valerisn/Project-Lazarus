#pragma once
// Console command registry. Lives in the loader so any mod can add commands
// and any front-end (the F6 console today, maybe a pipe/TCP thing later) can
// run them.

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lz {

// splits on whitespace, "double quotes" group, \" escapes a quote inside quotes
std::vector<std::string> tokenize(std::string_view line);

class CommandRegistry {
public:
    using Print = std::function<void(const std::string&)>;
    using Handler = std::function<void(const std::vector<std::string>& args, const Print& print)>;

    enum class Result { Ok, Empty, NotFound };

    // false if the name is invalid or already taken by another owner
    bool add(const std::string& name, const std::string& help, const std::string& owner, Handler fn);
    void remove_owner(const std::string& owner);

    // args[0] is the command name. unknown commands print suggestions.
    Result execute(std::string_view line, const Print& print) const;

    struct Entry {
        std::string help;
        std::string owner;
        Handler fn;
    };
    const std::map<std::string, Entry>& entries() const { return cmds_; }

private:
    std::map<std::string, Entry> cmds_;  // lowercase name -> entry, sorted for help output
};

bool is_valid_command_name(const std::string& name);

}  // namespace lz
