// F6 console.
//
// the window has its own thread so a slow frame never eats keystrokes.
// commands go to the main thread via run_on_main, output comes back through
// a locked queue + PostMessage. everything printed also goes to lazarus.log,
// so a pasted log shows exactly what the player saw.
//
// separate topmost window like the fps box, so no exclusive fullscreen either

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "commands.h"
#include "il2cpp.h"
#include "lazarus/lazarus_api.h"

namespace {

const LazarusApi* api = nullptr;
LazarusMod* self = nullptr;

struct Cfg {
    int key = VK_F6;
    int height_pct = 45;
    int opacity = 235;
    std::string autoexec_scene = "Main Menu";
    int autoexec_delay = 120;
    std::string autoexec;
    bool unlock_cursor = true;
} cfg;

const UINT WM_APP_OUTPUT = WM_APP + 1;

HWND g_wnd = nullptr, g_out = nullptr, g_in = nullptr, g_game = nullptr;
WNDPROC g_in_proc = nullptr;
HBRUSH g_bg = nullptr;
HFONT g_font = nullptr;
std::atomic<bool> g_stop{false};
HANDLE g_thread = nullptr;
bool g_open = false;

std::mutex g_out_mu;
std::deque<std::string> g_pending;
std::vector<std::string> g_history;
int g_hist_pos = 0;

// ------------------------------------------------------------- output side

void print_line(void*, const char* text) {
    api->log(self, LAZARUS_LOG_INFO, "%s", text);
    {
        std::lock_guard<std::mutex> g(g_out_mu);
        g_pending.emplace_back(text);
        if (g_pending.size() > 2000) g_pending.pop_front();  // console never opened, don't grow forever
    }
    if (g_wnd) PostMessageW(g_wnd, WM_APP_OUTPUT, 0, 0);
}

void flush_output() {
    std::deque<std::string> lines;
    {
        std::lock_guard<std::mutex> g(g_out_mu);
        lines.swap(g_pending);
    }
    if (lines.empty() || !g_out) return;
    std::wstring w;
    for (auto& l : lines) {
        int n = MultiByteToWideChar(CP_UTF8, 0, l.c_str(), (int)l.size(), nullptr, 0);
        std::wstring part((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, l.c_str(), (int)l.size(), part.data(), n);
        w += part + L"\r\n";
    }
    int len = GetWindowTextLengthW(g_out);
    if (len > 60000) {  // edit controls crawl past this, chop the oldest bit
        SendMessageW(g_out, EM_SETSEL, 0, 20000);
        SendMessageW(g_out, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = GetWindowTextLengthW(g_out);
    }
    SendMessageW(g_out, EM_SETSEL, len, len);
    SendMessageW(g_out, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());
    SendMessageW(g_out, WM_VSCROLL, SB_BOTTOM, 0);
}

// ------------------------------------------------------ main-thread tasks

void run_line_task(void* p) {
    auto* line = static_cast<std::string*>(p);
    api->execute_command(line->c_str(), &print_line, nullptr);
    delete line;
}

int32_t g_saved_lock = -1;
bool g_saved_visible = true;
int g_saved_run_bg = -1;
HANDLE g_prepared = nullptr;  // set when focus_task has run

// main thread, around open/close. runInBackground is the important bit: the
// console takes focus, and with it off unity stops ticking the moment the
// game loses focus, so your commands would just sit there until you closed
// the console. has to happen before focus moves, hence the event
void focus_task(void* p) {
    bool opening = p != nullptr;
    auto get_bg = reinterpret_cast<bool (*)()>(api->resolve_icall("UnityEngine.Application::get_runInBackground"));
    auto set_bg = reinterpret_cast<void (*)(bool)>(api->resolve_icall("UnityEngine.Application::set_runInBackground"));
    auto get_lock = reinterpret_cast<int32_t (*)()>(api->resolve_icall("UnityEngine.Cursor::get_lockState"));
    auto set_lock = reinterpret_cast<void (*)(int32_t)>(api->resolve_icall("UnityEngine.Cursor::set_lockState"));
    auto get_vis = reinterpret_cast<bool (*)()>(api->resolve_icall("UnityEngine.Cursor::get_visible"));
    auto set_vis = reinterpret_cast<void (*)(bool)>(api->resolve_icall("UnityEngine.Cursor::set_visible"));
    bool cursor_ok = cfg.unlock_cursor && get_lock && set_lock && get_vis && set_vis;
    if (opening) {
        if (get_bg && set_bg) {
            g_saved_run_bg = get_bg() ? 1 : 0;
            set_bg(true);
        }
        if (cursor_ok) {
            g_saved_lock = get_lock();
            g_saved_visible = get_vis();
            set_lock(0);
            set_vis(true);
        }
        SetEvent(g_prepared);
    } else {
        if (set_bg && g_saved_run_bg >= 0) set_bg(g_saved_run_bg == 1);
        g_saved_run_bg = -1;
        if (cursor_ok && g_saved_lock >= 0) {
            set_lock(g_saved_lock);
            set_vis(g_saved_visible);
        }
        g_saved_lock = -1;
    }
}

void submit(const std::wstring& wline) {
    int n = WideCharToMultiByte(CP_UTF8, 0, wline.c_str(), (int)wline.size(), nullptr, 0, nullptr, nullptr);
    std::string line((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wline.c_str(), (int)wline.size(), line.data(), n, nullptr, nullptr);
    if (line.find_first_not_of(" \t") == std::string::npos) return;
    if (g_history.empty() || g_history.back() != line) g_history.push_back(line);
    g_hist_pos = (int)g_history.size();

    // clear is ui only, don't bother the main thread
    if (line == "clear" || line == "cls") {
        SetWindowTextW(g_out, L"");
        return;
    }
    print_line(nullptr, ("> " + line).c_str());
    if (api->run_on_main(self, &run_line_task, new std::string(line)) != LAZARUS_OK)
        print_line(nullptr, "loader has no frame tick, can't run commands");
}

// -------------------------------------------------------------- window

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

void force_foreground(HWND w) {
    if (SetForegroundWindow(w)) return;
    // windows said no. the usual AttachThreadInput dance
    HWND fg = GetForegroundWindow();
    DWORD fg_tid = GetWindowThreadProcessId(fg, nullptr), me = GetCurrentThreadId();
    AttachThreadInput(me, fg_tid, TRUE);
    SetForegroundWindow(w);
    AttachThreadInput(me, fg_tid, FALSE);
}

void open_console() {
    if (!g_game) return;
    ResetEvent(g_prepared);
    if (api->run_on_main(self, &focus_task, (void*)1) == LAZARUS_OK)
        WaitForSingleObject(g_prepared, 500);  // if the game's hung, open anyway
    RECT cr;
    GetClientRect(g_game, &cr);
    POINT tl{0, 0};
    ClientToScreen(g_game, &tl);
    int h = cr.bottom * cfg.height_pct / 100;
    if (h < 160) h = 160;
    SetWindowPos(g_wnd, HWND_TOPMOST, tl.x, tl.y, cr.right, h, SWP_SHOWWINDOW);
    force_foreground(g_wnd);
    SetFocus(g_in);
    g_open = true;
}

void close_console(bool refocus_game) {
    if (!g_open) return;
    ShowWindow(g_wnd, SW_HIDE);
    g_open = false;
    if (refocus_game && g_game) force_foreground(g_game);
    api->run_on_main(self, &focus_task, nullptr);
}

LRESULT CALLBACK input_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            int len = GetWindowTextLengthW(w);
            std::wstring s((size_t)len, L'\0');
            GetWindowTextW(w, s.data(), len + 1);
            SetWindowTextW(w, L"");
            submit(s);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            close_console(true);
            return 0;
        }
        if ((wp == VK_UP || wp == VK_DOWN) && !g_history.empty()) {
            g_hist_pos += wp == VK_UP ? -1 : 1;
            if (g_hist_pos < 0) g_hist_pos = 0;
            if (g_hist_pos > (int)g_history.size()) g_hist_pos = (int)g_history.size();
            std::wstring s;
            if (g_hist_pos < (int)g_history.size()) {
                auto& h = g_history[(size_t)g_hist_pos];
                int n = MultiByteToWideChar(CP_UTF8, 0, h.c_str(), (int)h.size(), nullptr, 0);
                s.resize((size_t)n);
                MultiByteToWideChar(CP_UTF8, 0, h.c_str(), (int)h.size(), s.data(), n);
            }
            SetWindowTextW(w, s.c_str());
            SendMessageW(w, EM_SETSEL, s.size(), s.size());
            return 0;
        }
    }
    // or the edit control beeps at you
    if (msg == WM_CHAR && (wp == L'\r' || wp == 27 || wp == L'\t')) return 0;
    return CallWindowProcW(g_in_proc, w, msg, wp, lp);
}

LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: {
            int cw = LOWORD(lp), ch = HIWORD(lp);
            MoveWindow(g_out, 0, 0, cw, ch - 26, TRUE);
            MoveWindow(g_in, 0, ch - 24, cw, 24, TRUE);
            return 0;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, (HWND)lp == g_in ? RGB(255, 255, 255) : RGB(190, 230, 190));
            SetBkColor(dc, RGB(16, 18, 20));
            return (LRESULT)g_bg;
        }
        case WM_APP_OUTPUT:
            flush_output();
            return 0;
        case WM_ACTIVATE:
            // clicked the game or alt-tabbed, get out of the way
            if (LOWORD(wp) == WA_INACTIVE && g_open) close_console(false);
            return 0;
        case WM_CLOSE:
            close_console(true);
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

bool create_window() {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LazarusDevConsole";
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    g_bg = CreateSolidBrush(RGB(16, 18, 20));
    wc.hbrBackground = g_bg;
    RegisterClassExW(&wc);
    g_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"Lazarus console",
                            WS_POPUP | WS_BORDER, 0, 0, 800, 300, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_wnd) return false;
    SetLayeredWindowAttributes(g_wnd, 0, (BYTE)cfg.opacity, LWA_ALPHA);
    g_font = CreateFontW(17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH,
                         L"Consolas");
    g_out = CreateWindowExW(0, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0,
                            10, 10, g_wnd, nullptr, wc.hInstance, nullptr);
    g_in = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, g_wnd, nullptr,
                           wc.hInstance, nullptr);
    for (HWND h : {g_out, g_in}) SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    g_in_proc = (WNDPROC)SetWindowLongPtrW(g_in, GWLP_WNDPROC, (LONG_PTR)&input_proc);
    RECT r;
    GetClientRect(g_wnd, &r);
    SendMessageW(g_wnd, WM_SIZE, 0, MAKELPARAM(r.right, r.bottom));
    return true;
}

DWORD WINAPI ui_thread(LPVOID) {
    if (!create_window()) {
        api->log(self, LAZARUS_LOG_ERROR, "couldn't create console window (error %lu)", GetLastError());
        return 1;
    }
    bool was_down = false;
    while (!g_stop.load()) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 30, QS_ALLINPUT);
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (!g_game || !IsWindow(g_game)) {
            g_game = nullptr;
            EnumWindows(find_game, (LPARAM)&g_game);
        }
        // polling, not RegisterHotKey - that would steal F6 from every other
        // app while the game's open
        bool down = (GetAsyncKeyState(cfg.key) & 0x8000) != 0;
        if (down && !was_down) {
            HWND fg = GetForegroundWindow();
            if (g_open && fg == g_wnd) close_console(true);
            else if (!g_open && g_game && fg == g_game) open_console();
        }
        was_down = down;
    }
    DestroyWindow(g_wnd);
    return 0;
}

// ------------------------------------------------------------- autoexec

int g_countdown = -1;
bool g_autoexec_done = false;

void on_scene(void*, const char* name, int, int) {
    if (g_autoexec_done || cfg.autoexec.empty()) return;
    if (_stricmp(name, cfg.autoexec_scene.c_str()) != 0) return;
    g_countdown = cfg.autoexec_delay;
    api->log(self, LAZARUS_LOG_INFO, "autoexec: '%s' loaded, running in %d frames", name, cfg.autoexec_delay);
}

void on_update(void*) {
    if (g_countdown < 0 || --g_countdown > 0) return;
    g_countdown = -1;
    g_autoexec_done = true;  // once, or "load X" from the main menu loops forever
    std::string all = cfg.autoexec;
    size_t start = 0;
    while (start <= all.size()) {
        size_t semi = all.find(';', start);
        if (semi == std::string::npos) semi = all.size();
        std::string cmd = all.substr(start, semi - start);
        if (cmd.find_first_not_of(" \t") != std::string::npos) {
            print_line(nullptr, ("autoexec> " + cmd).c_str());
            api->execute_command(cmd.c_str(), &print_line, nullptr);
        }
        start = semi + 1;
    }
}

void echo_cmd(void*, int argc, const char** argv, LazarusPrintFn print, void* ctx) {
    std::string s;
    for (int i = 1; i < argc; ++i) s += std::string(i > 1 ? " " : "") + argv[i];
    print(ctx, s.c_str());
}

int parse_key(const char* s) {
    if ((s[0] == 'F' || s[0] == 'f') && s[1]) {
        int n = std::atoi(s + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (!_stricmp(s, "grave") || !_stricmp(s, "tilde") || !strcmp(s, "`")) return VK_OEM_3;
    if (!_stricmp(s, "insert")) return VK_INSERT;
    return 0;
}

}  // namespace

extern "C" __declspec(dllexport) int lazarus_mod_init(LazarusMod* m, const LazarusApi* a) {
    api = a;
    self = m;
    if (a->api_version < 2 || a->struct_size < sizeof(LazarusApi)) {
        a->log(m, LAZARUS_LOG_ERROR, "needs loader API v2");
        return 1;
    }
    // no il2cpp = no game commands, but help/mods/echo and other mods' stuff still work
    bool il2_ok = il2::init(a);
    if (!il2_ok)
        a->log(m, LAZARUS_LOG_WARN, "GameAssembly.dll is missing %s - level/cheat/inspect commands disabled",
               il2::missing());

    char buf[512];
    if (a->config_get_string(m, "console", "key", buf, sizeof buf) && buf[0]) {
        int k = parse_key(buf);
        if (k) cfg.key = k;
        else a->log(m, LAZARUS_LOG_WARN, "config.ini: key '%s' unknown (use F1-F12, grave, insert), using F6", buf);
    }
    cfg.height_pct = (int)a->config_get_int(m, "console", "height_percent", 45);
    if (cfg.height_pct < 15 || cfg.height_pct > 95) cfg.height_pct = 45;
    cfg.opacity = (int)a->config_get_int(m, "console", "opacity", 235);
    if (cfg.opacity < 60 || cfg.opacity > 255) cfg.opacity = 235;
    cfg.unlock_cursor = a->config_get_bool(m, "console", "unlock_cursor", 1) != 0;
    if (a->config_get_string(m, "autoexec", "commands", buf, sizeof buf)) cfg.autoexec = buf;
    if (a->config_get_string(m, "autoexec", "after_scene", buf, sizeof buf) && buf[0]) cfg.autoexec_scene = buf;
    cfg.autoexec_delay = (int)a->config_get_int(m, "autoexec", "delay_frames", 120);
    if (cfg.autoexec_delay < 1) cfg.autoexec_delay = 1;

    if (il2_ok) con::register_all(a, m);
    a->register_command(m, "echo", "print text", &echo_cmd, nullptr);
    if (a->on_update(m, &on_update, nullptr) != LAZARUS_OK || a->on_scene_loaded(m, &on_scene, nullptr) != LAZARUS_OK)
        a->log(m, LAZARUS_LOG_WARN, "no frame tick, autoexec disabled");

    print_line(nullptr, "Project Lazarus dev console. 'help' lists commands, Esc closes, Up/Down for history.");
    g_prepared = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, ui_thread, nullptr, 0, nullptr);
    a->log(m, LAZARUS_LOG_INFO, "ready, press %s in game%s", cfg.key == VK_F6 ? "F6" : "the console key",
           cfg.autoexec.empty() ? "" : " (autoexec set)");
    return 0;
}

extern "C" __declspec(dllexport) void lazarus_mod_shutdown(LazarusMod*) {
    g_stop = true;
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
    }
}
