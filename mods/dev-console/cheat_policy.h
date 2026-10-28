#pragma once
// the cheat object can delete or overwrite your main save (CallOnDeleteMainSave,
// CreateNewSave...). being one typo away from losing a 20 hour save is not a
// dev tool, it's a trap. those need cheat-unsafe

#include <string>

namespace con {

inline bool is_destructive_cheat(const std::string& method) {
    std::string m;
    for (char c : method) m += (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    return m.find("save") != std::string::npos || m.find("delete") != std::string::npos;
}

}  // namespace con
