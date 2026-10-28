#pragma once
#include <windows.h>

namespace lzrt {

void set_proxy_module(HMODULE self);

// can run under the loader lock (DllMain), so raw win32 file writes only.
// lines also get buffered and replayed into lazarus.log once that exists
void bootstrap_note(const char* msg);

// main thread, after the real il2cpp_init
void on_il2cpp_ready(HMODULE game_assembly);
// main thread, before the real il2cpp_shutdown
void on_il2cpp_shutdown();

}  // namespace lzrt
