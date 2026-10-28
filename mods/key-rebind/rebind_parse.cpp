#include "rebind_parse.h"

#include "core/ini.h"

namespace rebind {

static bool looks_like_path(const std::string& p) {
    // Input System control paths always start with a <Layout>, e.g. <Keyboard>/f,
    // <Mouse>/leftButton, <Gamepad>/buttonSouth
    auto close = p.find('>');
    return p.size() > 3 && p[0] == '<' && close != std::string::npos && close > 1 && close + 1 < p.size() &&
           p[close + 1] == '/';
}

bool parse(const std::string& key, const std::string& value, Rebind& out, std::string& err) {
    out = {};
    out.action = lz::trim(key);
    if (out.action.empty()) {
        err = "empty action name";
        return false;
    }
    auto arrow = value.find("->");
    if (arrow == std::string::npos) {
        err = "expected '<from> -> <to>', e.g. <Keyboard>/e -> <Keyboard>/f";
        return false;
    }
    out.from = lz::trim(value.substr(0, arrow));
    out.to = lz::trim(value.substr(arrow + 2));
    if (!looks_like_path(out.from)) {
        err = "'" + out.from + "' isn't a control path like <Keyboard>/e";
        return false;
    }
    if (!looks_like_path(out.to)) {
        err = "'" + out.to + "' isn't a control path like <Keyboard>/f";
        return false;
    }
    return true;
}

}  // namespace rebind
