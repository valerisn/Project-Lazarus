#pragma once
#include <windows.h>

namespace lzrt {

// swaps one IAT slot. matches by name through the INT, so it doesn't care
// whether the loader already bound the IAT
bool patch_iat(HMODULE module, const char* import_dll, const char* func, void* replacement, void** original);

}  // namespace lzrt
