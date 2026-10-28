#pragma once
#include <string>

namespace rebind {

// one line of [bindings]:  <action> = <from control path> -> <to control path>
//   interact = <Keyboard>/e -> <Keyboard>/f
struct Rebind {
    std::string action;
    std::string from;
    std::string to;
};

// false + err when the line can't be used
bool parse(const std::string& key, const std::string& value, Rebind& out, std::string& err);

}  // namespace rebind
