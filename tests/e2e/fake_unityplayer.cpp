// fake UnityPlayer.dll. the import table is the point: VERSION.dll static
// import (so the proxy loads) and il2cpp_* through its own GetProcAddress

#include <windows.h>
#include <d3d11.h>

#include <cstdio>
#include <initializer_list>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "d3d11.lib")

using init_t = int (*)(const char*);
using shutdown_t = void (*)();
using load_scene_t = void (*)(int, int);
using get_value_t = int (*)();
using state_t = int (*)(const char*);

// n real d3d11 presents on a hidden window, so the fps hook runs against real
// dxgi. WARP for CI. -1 if no device
static int render_frames(int n) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FakeUnityRender";
    RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!w) return -1;
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = w;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = E_FAIL;
    for (auto type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                           nullptr, &ctx);
        if (SUCCEEDED(hr)) break;
    }
    int done = -1;
    if (SUCCEEDED(hr)) {
        done = 0;
        for (int i = 0; i < n; ++i, ++done) sc->Present(0, 0);
        sc->Present(0, DXGI_PRESENT_TEST);  // must not be counted
        sc->Release();
        ctx->Release();
        dev->Release();
    }
    DestroyWindow(w);
    return done;
}

// writes key=value lines for the runner to check
extern "C" __declspec(dllexport) int fake_unity_main(const wchar_t* report_path) {
    FILE* r = _wfopen(report_path, L"w");
    if (!r) return 90;

    // goes through our forwarding stub
    DWORD dummy = 0;
    DWORD vsize = GetFileVersionInfoSizeW(L"kernel32.dll", &dummy);
    std::fprintf(r, "version_forward=%d\n", vsize > 0 ? 1 : 0);

    HMODULE ga = LoadLibraryW(L"GameAssembly.dll");
    if (!ga) return 91;
    auto init = reinterpret_cast<init_t>(GetProcAddress(ga, "il2cpp_init"));
    auto shutdown = reinterpret_cast<shutdown_t>(GetProcAddress(ga, "il2cpp_shutdown"));
    auto load_scene = reinterpret_cast<load_scene_t>(GetProcAddress(ga, "fake_engine_load_scene"));
    auto get_value = reinterpret_cast<get_value_t>(GetProcAddress(ga, "fake_game_get_value"));
    auto state = reinterpret_cast<state_t>(GetProcAddress(ga, "fake_state"));
    if (!init || !shutdown || !load_scene || !get_value || !state) return 92;

    // if the hook worked, this il2cpp_init lives in version.dll
    HMODULE owner = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(init), &owner);
    wchar_t owner_name[MAX_PATH] = L"?";
    if (owner) GetModuleFileNameW(owner, owner_name, MAX_PATH);
    const wchar_t* base = wcsrchr(owner_name, L'\\');
    std::fprintf(r, "init_owner=%ls\n", base ? base + 1 : owner_name);
    std::fflush(r);

    int rc = init("IL2CPP Root Domain");
    std::fprintf(r, "init_rc=%d\nreal_init_called=%d\n", rc, state("init"));
    std::fflush(r);

    auto meddle = reinterpret_cast<void (*)()>(GetProcAddress(ga, "fake_game_meddle"));
    std::fprintf(r, "value_before_scene=%d\n", get_value());
    load_scene(1, 0);  // MainMenu, single
    meddle();          // game messes with fps/vsync
    load_scene(2, 1);  // Prison, additive
    load_scene(1, 0);  // back to MainMenu, single
    std::fprintf(r, "frames_rendered=%d\n", render_frames(60));
    auto frame = reinterpret_cast<void (*)()>(GetProcAddress(ga, "fake_engine_frame"));
    for (int i = 0; i < 5; ++i) {
        frame();
        Sleep(5);
    }
    std::fprintf(r, "notify_calls=%d\n", state("notify_calls"));
    auto tick = reinterpret_cast<int (*)(bool, int)>(GetProcAddress(ga, "fake_game_tick"));
    auto reset = reinterpret_cast<int (*)(int)>(GetProcAddress(ga, "fake_game_reset"));
    std::fprintf(r, "tick=%d\nreset=%d\n", tick(true, 21), reset(7));
    std::fprintf(r, "scene_listener_calls=%d\n", state("scene_listener"));
    std::fprintf(r, "value_in_game=%d\n", get_value());
    for (const char* k : {"target_frame_rate", "vsync", "quality", "lod_x100", "aniso", "res_w", "res_h", "res_mode", "res_hz"})
        std::fprintf(r, "%s=%d\n", k, state(k));
    std::fflush(r);

    shutdown();
    std::fprintf(r, "real_shutdown_called=%d\n", state("shutdown"));
    std::fprintf(r, "value_after_shutdown=%d\n", get_value());
    std::fprintf(r, "finished=1\n");
    std::fclose(r);
    return 0;
}
