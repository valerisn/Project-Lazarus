#pragma once
// Which LucilleCheats methods are too dangerous for the plain `cheat` command.
//
// The developers' cheat object can delete or overwrite the player's main
// save (CallOnDeleteMainSave, CreateNewSave, CallOnCreateMainSave...). One
// typo away from losing a 20 hour playthrough is not a dev tool, it's a trap,
// so anything touching saves or deleting things needs `cheat-unsafe`.
// (security review #4)

#include <string>

namespace con {

inline bool is_destructive_cheat(const std::string& method) {
    std::string m;
    for (char c : method) m += (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    return m.find("save") != std::string::npos || m.find("delete") != std::string::npos;
}

}  // namespace con
