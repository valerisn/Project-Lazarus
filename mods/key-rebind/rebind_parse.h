#pragma once
#include <string>

namespace rebind {

// one [bindings] line:  interact = <Keyboard>/e -> <Keyboard>/f
struct Rebind {
    std::string action;
    std::string from;
    std::string to;
};

bool parse(const std::string& key, const std::string& value, Rebind& out, std::string& err);

}  // namespace rebind
