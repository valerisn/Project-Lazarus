// fps counter. counts frames at IDXGISwapChain::Present, which every d3d11
// and d3d12 frame goes through.
//
// the box is its own topmost window instead of being drawn into the frame.
// drawing in-frame means messing with render state for two APIs; a separate
// window can't break rendering, worst case the box just doesn't show up.
// downside is no box in exclusive fullscreen. unity defaults to borderless so meh

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "lazarus/lazarus_api.h"

namespace {

const LazarusApi* api = nullptr;
LazarusMod* self = nullptr;

std::atomic<uint64_t> g_frames{0};
std::atomic<bool> g_stop{false};
std::atomic<int> g_fps_x10{-1};  // -1 until the first frame
HANDLE g_thread = nullptr;

struct Cfg {
    bool overlay = true;
    int corner = 0;  // 0 tl, 1 tr, 2 bl, 3 br
    bool title = false;
    int log_interval = 10;
} cfg;

// ----------------------------------------------------------- present hooks

using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
Present_t g_orig_present = nullptr;
Present1_t g_orig_present1 = nullptr;
void* g_hooked[4] = {};
int g_hook_count = 0;

// in case dxgi calls Present1 through Present internally (or the other way
// round), don't count one frame twice
thread_local int t_depth = 0;

// time of the last counted frame. v1 counted frames per 0.5s window, which
// at 45fps is 22 or 23 frames, so it bounced between 44 and 46. using the
// actual time between frames fixes that
std::atomic<int64_t> g_last_present_qpc{0};

void count_frame() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    g_frames.fetch_add(1, std::memory_order_relaxed);
    g_last_present_qpc.store(t.QuadPart, std::memory_order_release);
}

HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (t_depth == 0 && !(flags & DXGI_PRESENT_TEST)) count_frame();
    ++t_depth;
    HRESULT hr = g_orig_present(sc, sync, flags);
    --t_depth;
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_present1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    if (t_depth == 0 && !(flags & DXGI_PRESENT_TEST)) count_frame();
    ++t_depth;
    HRESULT hr = g_orig_present1(sc, sync, flags, p);
    --t_depth;
    return hr;
}

bool already_hooked(void* p) {
    for (int i = 0; i < g_hook_count; ++i)
        if (g_hooked[i] == p) return true;
    return false;
}

void hook_vtable(void** vt, const char* api_name) {
    // vtable slots: Present = 8, Present1 = 22
    void* present = vt[8];
    void* present1 = vt[22];
    if (!already_hooked(present) && g_hook_count < 4 &&
        api->hook_create(self, present, (void*)&hk_present, (void**)&g_orig_present) == LAZARUS_OK) {
        g_hooked[g_hook_count++] = present;
        api->log(self, LAZARUS_LOG_DEBUG, "hooked %s Present at %p", api_name, present);
    }
    if (!already_hooked(present1) && g_hook_count < 4 &&
        api->hook_create(self, present1, (void*)&hk_present1, (void**)&g_orig_present1) == LAZARUS_OK) {
        g_hooked[g_hook_count++] = present1;
        api->log(self, LAZARUS_LOG_DEBUG, "hooked %s Present1 at %p", api_name, present1);
    }
}

HWND make_dummy_window() {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LazarusFpsDummy";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance,
                           nullptr);
}

// throwaway device + swapchain, all we want is the vtable. loaded dynamically
// so no d3d12.dll doesn't mean no mod
void hook_d3d11(HWND wnd) {
    // System32 only. none of these are KnownDLLs, a bare name would look in
    // the game folder first
    HMODULE d3d11 = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!d3d11) return;
    auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
        GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain"));
    if (!create) return;
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = wnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = E_FAIL;
    // WARP for boxes without a GPU, like CI
    for (auto type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = create(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, nullptr, &ctx);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) {
        api->log(self, LAZARUS_LOG_WARN, "couldn't create a D3D11 probe device (0x%08lX)", (unsigned long)hr);
        return;
    }
    hook_vtable(*reinterpret_cast<void***>(sc), "D3D11");
    sc->Release();
    ctx->Release();
    dev->Release();
}

void hook_d3d12(HWND wnd) {
    HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!d3d12 || !dxgi) return;
    auto create_device = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12, "D3D12CreateDevice"));
    auto create_factory = reinterpret_cast<HRESULT(WINAPI*)(REFIID, void**)>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    if (!create_device || !create_factory) return;

    ID3D12Device* dev = nullptr;
    if (FAILED(create_device(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev))) return;
    ID3D12CommandQueue* q = nullptr;
    D3D12_COMMAND_QUEUE_DESC qd{};
    IDXGIFactory4* fac = nullptr;
    IDXGISwapChain1* sc = nullptr;
    if (SUCCEEDED(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q)) &&
        SUCCEEDED(create_factory(__uuidof(IDXGIFactory4), (void**)&fac))) {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = sd.Height = 64;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (SUCCEEDED(fac->CreateSwapChainForHwnd(q, wnd, &sd, nullptr, nullptr, &sc)))
            hook_vtable(*reinterpret_cast<void***>(sc), "D3D12");
    }
    if (sc) sc->Release();
    if (fac) fac->Release();
    if (q) q->Release();
    dev->Release();
}

// ----------------------------------------------------------------- overlay

HWND g_game = nullptr;
wchar_t g_orig_title[256] = {};

BOOL CALLBACK find_game(HWND w, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w)) return TRUE;
    wchar_t cls[64];
    GetClassNameW(w, cls, 64);
    if (wcscmp(cls, L"UnityWndClass") != 0) return TRUE;
    *reinterpret_cast<HWND*>(out) = w;
    return FALSE;
}

LRESULT CALLBACK overlay_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &r, bg);
        DeleteObject(bg);
        static HFONT font = CreateFontW(20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                        FIXED_PITCH, L"Consolas");
        HGDIOBJ old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        int v = g_fps_x10.load();
        wchar_t text[32];
        if (v < 0) swprintf_s(text, L"-- FPS");
        else swprintf_s(text, L"%d FPS", (v + 5) / 10);
        // green 55+, yellow 30+, red under that
        SetTextColor(dc, v >= 550 ? RGB(80, 255, 80) : v >= 300 ? RGB(255, 220, 60) : RGB(255, 80, 80));
        DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        EndPaint(w, &ps);
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(w, msg, wp, lp);
}

HWND make_overlay() {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = overlay_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LazarusFpsOverlay";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    // clicks go through it and it never steals focus
    HWND w = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"Lazarus FPS", WS_POPUP, 0, 0, 96, 30, nullptr, nullptr, wc.hInstance,
                             nullptr);
    if (w) SetLayeredWindowAttributes(w, 0, 190, LWA_ALPHA);
    return w;
}

void place_overlay(HWND ov) {
    RECT cr;
    if (!GetClientRect(g_game, &cr)) return;
    POINT tl{0, 0}, br{cr.right, cr.bottom};
    ClientToScreen(g_game, &tl);
    ClientToScreen(g_game, &br);
    const int W = 96, H = 30, M = 8;
    int x = (cfg.corner == 1 || cfg.corner == 3) ? br.x - W - M : tl.x + M;
    int y = (cfg.corner >= 2) ? br.y - H - M : tl.y + M;
    // hide it when the game isn't in front, or it floats over your browser
    bool show = GetForegroundWindow() == g_game && !IsIconic(g_game);
    SetWindowPos(ov, HWND_TOPMOST, x, y, W, H, SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

// render thread writes count then time, so retry until the count holds still
struct Sample {
    uint64_t frames;
    int64_t qpc;
};
Sample sample() {
    for (;;) {
        uint64_t a = g_frames.load(std::memory_order_acquire);
        int64_t t = g_last_present_qpc.load(std::memory_order_acquire);
        if (g_frames.load(std::memory_order_acquire) == a) return {a, t};
    }
}

double rate(const Sample& from, const Sample& to, double qpc_freq) {
    if (to.frames <= from.frames || to.qpc <= from.qpc) return -1;
    return (double)(to.frames - from.frames) * qpc_freq / (double)(to.qpc - from.qpc);
}

DWORD WINAPI overlay_thread(LPVOID) {
    HWND ov = nullptr;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const double f = (double)freq.QuadPart;
    Sample last = sample(), log_last = last;
    ULONGLONG last_tick = GetTickCount64(), log_tick = last_tick;

    while (!g_stop.load()) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        Sleep(250);

        ULONGLONG now = GetTickCount64();
        if (now - last_tick >= 500) {
            Sample s = sample();
            double r = rate(last, s, f);
            if (r >= 0) g_fps_x10 = (int)(r * 10.0 + 0.5);
            else if (g_fps_x10.load() >= 0) g_fps_x10 = 0;  // no frames = alt-tabbed or hung
            last = s;
            last_tick = now;
        }
        if (cfg.log_interval > 0 && now - log_tick >= (ULONGLONG)cfg.log_interval * 1000) {
            Sample s = sample();
            double r = rate(log_last, s, f);
            if (r >= 0)
                api->log(self, LAZARUS_LOG_INFO, "average %.1f fps over the last %ds (%llu frames total)", r,
                         cfg.log_interval, (unsigned long long)s.frames);
            log_last = s;
            log_tick = now;
        }

        if (!g_game || !IsWindow(g_game)) {
            g_game = nullptr;
            EnumWindows(find_game, (LPARAM)&g_game);
            if (g_game && cfg.title) GetWindowTextW(g_game, g_orig_title, 256);
        }
        if (!g_game) continue;

        if (cfg.overlay) {
            if (!ov) ov = make_overlay();
            if (ov) {
                place_overlay(ov);
                InvalidateRect(ov, nullptr, FALSE);
            }
        }
        if (cfg.title && g_fps_x10.load() >= 0) {
            wchar_t t[320];
            swprintf_s(t, L"%s  |  %d FPS", g_orig_title, (g_fps_x10.load() + 5) / 10);
            // SetWindowText hangs forever if the game's main thread is stuck,
            // and then shutdown would too
            DWORD_PTR r;
            SendMessageTimeoutW(g_game, WM_SETTEXT, 0, (LPARAM)t, SMTO_ABORTIFHUNG, 200, &r);
        }
    }
    if (ov) DestroyWindow(ov);
    return 0;
}

}  // namespace

extern "C" __declspec(dllexport) int lazarus_mod_init(LazarusMod* m, const LazarusApi* a) {
    api = a;
    self = m;
    if (a->struct_size < sizeof(LazarusApi)) return 1;

    cfg.overlay = a->config_get_bool(m, "overlay", "enabled", 1) != 0;
    char corner[32] = "top-left";
    a->config_get_string(m, "overlay", "corner", corner, sizeof corner);
    if (!_stricmp(corner, "top-right")) cfg.corner = 1;
    else if (!_stricmp(corner, "bottom-left")) cfg.corner = 2;
    else if (!_stricmp(corner, "bottom-right")) cfg.corner = 3;
    else if (_stricmp(corner, "top-left") && corner[0])
        a->log(m, LAZARUS_LOG_WARN, "config.ini: corner '%s' unknown, using top-left", corner);
    cfg.title = a->config_get_bool(m, "title", "enabled", 0) != 0;
    long long li = a->config_get_int(m, "log", "interval_seconds", 10);
    cfg.log_interval = li < 0 ? 0 : li > 3600 ? 3600 : (int)li;

    HWND dummy = make_dummy_window();
    if (!dummy) {
        a->log(m, LAZARUS_LOG_ERROR, "couldn't create probe window (error %lu)", GetLastError());
        return 2;
    }
    hook_d3d11(dummy);
    hook_d3d12(dummy);
    DestroyWindow(dummy);
    if (!g_hook_count) {
        a->log(m, LAZARUS_LOG_ERROR, "couldn't hook DXGI Present, FPS counter disabled");
        return 3;
    }
    g_thread = CreateThread(nullptr, 0, overlay_thread, nullptr, 0, nullptr);
    a->log(m, LAZARUS_LOG_INFO, "counting frames (%d present hook(s)); overlay %s, window title %s, log every %ds",
           g_hook_count, cfg.overlay ? "on" : "off", cfg.title ? "on" : "off", cfg.log_interval);
    return 0;
}

extern "C" __declspec(dllexport) void lazarus_mod_shutdown(LazarusMod*) {
    g_stop = true;
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_game && cfg.title && g_orig_title[0]) {
        DWORD_PTR r;
        SendMessageTimeoutW(g_game, WM_SETTEXT, 0, (LPARAM)g_orig_title, SMTO_ABORTIFHUNG, 200, &r);
    }
    api->log(self, LAZARUS_LOG_INFO, "frames presented this session: %llu", (unsigned long long)g_frames.load());
}
