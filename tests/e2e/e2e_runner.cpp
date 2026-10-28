// End-to-end test runner. Lives in the staged "game folder" next to
// fake_game.exe / UnityPlayer.dll / GameAssembly.dll / version.dll (the real
// proxy) and launches the fake game once per scenario, since the proxy's
// DllMain only runs once per process.

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path g_stage;
int g_failures = 0;
std::string g_current;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL [%s] %s\n", g_current.c_str(), what.c_str());
        ++g_failures;
    }
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void spit(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

struct Run {
    DWORD exit_code = 0xFFFFFFFF;
    std::map<std::string, std::string> report;
    std::string log, bootstrap, events;
    bool has(const std::string& needle) const { return log.find(needle) != std::string::npos; }
    std::string r(const std::string& k) const {
        auto it = report.find(k);
        return it == report.end() ? "<missing>" : it->second;
    }
};

fs::path lz() { return g_stage / "Lazarus"; }
fs::path mod_dir() { return lz() / "mods" / "test-mod"; }

void reset(bool keep_state) {
    std::error_code ec;
    fs::remove_all(lz() / "logs", ec);
    if (!keep_state) fs::remove_all(lz() / "state", ec);
    fs::remove(mod_dir() / "events.txt", ec);
    fs::remove(g_stage / "report.txt", ec);
    fs::remove_all(lz() / "mods" / "broken", ec);
    spit(mod_dir() / "mod.ini",
         "[mod]\nid = test-mod\nname = E2E Test Mod\nversion = 1.0.0\ndll = test_mod.dll\napi_version = 1\n");
}

void configure(const std::string& loader_ini, const std::string& behavior) {
    spit(lz() / "lazarus.ini", loader_ini);
    spit(mod_dir() / "config.ini",
         "[test]\nbehavior = " + behavior + "\ntarget_fps = 90\nnot_a_bool = maybe\n");
}

Run launch(const wchar_t* env_safe_mode = nullptr) {
    Run run;
    SetEnvironmentVariableW(L"LAZARUS_SAFE_MODE", env_safe_mode);
    std::wstring cmd = L"\"" + (g_stage / "fake_game.exe").wstring() + L"\" \"" + (g_stage / "report.txt").wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, g_stage.c_str(), &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, 30000) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 0xDEAD);
        GetExitCodeProcess(pi.hProcess, &run.exit_code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    SetEnvironmentVariableW(L"LAZARUS_SAFE_MODE", nullptr);

    std::istringstream rep(slurp(g_stage / "report.txt"));
    for (std::string line; std::getline(rep, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // fake engine writes in text mode
        auto eq = line.find('=');
        if (eq != std::string::npos) run.report[line.substr(0, eq)] = line.substr(eq + 1);
    }
    run.log = slurp(lz() / "logs" / "lazarus.log");
    run.bootstrap = slurp(lz() / "logs" / "bootstrap.log");
    run.events = slurp(mod_dir() / "events.txt");
    run.events.erase(std::remove(run.events.begin(), run.events.end(), '\r'), run.events.end());
    return run;
}

const char* kEnabled = "[loader]\nlog_level = debug\n[mods]\nenabled = test-mod\n";

void scenario(const char* name, std::function<void()> fn) {
    g_current = name;
    int before = g_failures;
    fn();
    std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", name);
}

void dump_on_fail(const Run& run) {
    if (!g_failures) return;
    std::printf("---- lazarus.log ----\n%s\n---- bootstrap.log ----\n%s\n---- events ----\n%s\n", run.log.c_str(),
                run.bootstrap.c_str(), run.events.c_str());
}

}  // namespace

int wmain() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_stage = fs::path(self).parent_path();

    scenario("happy path: proxy boots, mod hooks, scenes dispatch, clean shutdown", [] {
        reset(false);
        configure(kEnabled, "normal");
        auto run = launch();
        expect(run.exit_code == 0, "exit code " + std::to_string(run.exit_code));
        expect(run.r("version_forward") == "1", "version.dll exports forward to System32");
        // loader keeps the import's spelling, so this comes back as VERSION.dll
        expect(_stricmp(run.r("init_owner").c_str(), "version.dll") == 0,
               "il2cpp_init intercepted, owner=" + run.r("init_owner"));
        expect(run.r("real_init_called") == "1", "real il2cpp_init still runs");
        expect(run.r("value_before_scene") == "42", "hook active right after init");
        expect(run.r("scene_listener_calls") == "3", "game's own scene listener still runs");
        expect(run.r("value_in_game") == "42", "gameplay hook active");
        expect(run.r("target_frame_rate") == "90", "icall from mod config applied");
        expect(run.r("real_shutdown_called") == "1", "real il2cpp_shutdown still runs");
        expect(run.r("value_after_shutdown") == "1", "hooks removed at shutdown");
        expect(run.events == "init\nscene:MainMenu:1:0\nscene:Prison:2:1\nscene:MainMenu:1:0\nshutdown\n",
               "event order: " + run.events);
        expect(run.bootstrap.find("intercepted il2cpp_init") != std::string::npos, "bootstrap.log written");
        expect(run.has("scene load hook installed"), "scene hook log");
        expect(run.has("[mod:test-mod] test mod init"), "mod log tag");
        expect(run.has("loaded E2E Test Mod v1.0.0"), "load log");
        expect(run.has("not_a_bool = 'maybe' is not true/false"), "bad config value warned");
        expect(run.has("is not one Lazarus has been tested on"), "unknown build warned");
        expect(run.has("startup complete: 1 mod(s) active"), "startup summary");
        expect(run.has("shutdown complete"), "shutdown log");

        // v2: frame tick, main-thread queue, commands
        expect(run.r("notify_calls") == "10", "engine pumped input twice per frame: " + run.r("notify_calls"));
        expect(run.has("frame tick hooked (input: yes"), "tick hook");
        expect(run.has("v2 update callbacks: 5"), "on_update gated to once per frame (5 frames, 10 pumps)");
        expect(run.has("v2 task ran on main thread"), "run_on_main from a worker thread lands on main");
        expect(run.has("v2 commands: rc=0,2,0 printed=[echo:hello world 2"), "command args + quoting");
        expect(run.has("unknown command 'no.such.command'"), "unknown command hint");
        expect(run.has("command 'test.crash' crashed (exception 0xC0000005)"), "crashing command contained");
        expect(run.has("could not register command 'help'"), "can't steal another owner's command");

        // regression from the first live run: some non-Unity process in the
        // game folder (crash handler) loaded the proxy after exit and wiped
        // bootstrap.log. this runner isn't a Unity process either.
        HMODULE proxy = LoadLibraryW((g_stage / "version.dll").c_str());
        expect(proxy != nullptr, "proxy loads in a non-Unity process");
        expect(slurp(lz() / "logs" / "bootstrap.log") == run.bootstrap, "non-Unity process left bootstrap.log alone");
        if (proxy) FreeLibrary(proxy);
        dump_on_fail(run);
    });

    scenario("mod not enabled: game runs untouched", [] {
        reset(false);
        configure("[mods]\nenabled =\n", "normal");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.r("value_in_game") == "1", "no hook");
        expect(run.events.empty(), "mod never ran");
        expect(run.has("skipping test-mod: not enabled"), "skip reason logged");
    });

    scenario("safe mode via environment", [] {
        reset(false);
        configure(kEnabled, "normal");
        auto run = launch(L"1");
        expect(run.r("value_in_game") == "1", "no hook");
        expect(run.events.empty(), "mod never ran");
        expect(run.has("safe mode: LAZARUS_SAFE_MODE=1"), "safe mode logged");
    });

    scenario("loader disabled in config", [] {
        reset(false);
        configure("[loader]\nenabled = false\n[mods]\nenabled = test-mod\n", "normal");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.r("value_in_game") == "1", "no hook");
        expect(run.has("doing nothing"), "disabled logged");
        expect(!run.has("scene loaded"), "scene hook not installed");
    });

    scenario("mod init returns error: hooks rolled back", [] {
        reset(false);
        configure(kEnabled, "fail_init");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.r("value_in_game") == "1", "hook removed after failed init");
        expect(run.events == "init\n", "no scene callbacks for failed mod: " + run.events);
        expect(run.has("init returned 5"), "failure logged");
        expect(run.has("startup complete: 0 mod(s) active"), "summary");
    });

    scenario("mod init throws: caught, game keeps running", [] {
        reset(false);
        configure(kEnabled, "throw_init");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.r("value_in_game") == "1", "hook removed after crash");
        expect(run.has("crashed during init (exception 0xE0000001)"), "exception logged");
    });

    scenario("scene callback access violation: callback disabled, game keeps running", [] {
        reset(false);
        configure(kEnabled, "crash_in_scene");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.r("scene_listener_calls") == "3", "game listener unaffected");
        expect(run.events == "init\nscene:MainMenu:1:0\nshutdown\n", "second scene not dispatched to dead cb: " + run.events);
        expect(run.has("scene callback crashed (exception 0xC0000005)"), "AV logged");
    });

    scenario("hard crash during mod init leaves boot lock", [] {
        reset(false);
        configure(kEnabled, "exit_in_init");
        auto run = launch();
        expect(run.exit_code == 3, "process died with mod's exit code");
        expect(run.r("finished") == "<missing>", "game did not finish");
        expect(fs::exists(lz() / "state" / "boot.lock"), "boot.lock survived");
    });

    scenario("next launch quarantines the crashing mod", [] {
        reset(true);  // keep state from the crash
        configure(kEnabled, "normal");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.r("value_in_game") == "1", "quarantined mod not loaded");
        expect(run.has("now quarantined"), "quarantine logged");
        expect(run.has("skipping test-mod: quarantined"), "skip reason");
        expect(slurp(lz() / "state" / "quarantine.txt").find("test-mod |") != std::string::npos, "quarantine file");

        // user deletes quarantine.txt -> mod loads again
        fs::remove(lz() / "state" / "quarantine.txt");
        reset(true);
        configure(kEnabled, "normal");
        auto again = launch();
        expect(again.r("value_in_game") == "42", "mod back after un-quarantine");
    });

    scenario("corrupted manifest in another mod doesn't block good mods", [] {
        reset(false);
        configure("[mods]\nenabled = broken, test-mod, ghost\n", "normal");
        spit(lz() / "mods" / "broken" / "mod.ini", "\x01\x02 this is not an ini [[[\n");
        auto run = launch();
        expect(run.r("value_in_game") == "42", "good mod loaded");
        expect(run.has("skipping broken: invalid mod.ini"), "broken mod reported");
        expect(run.has("'ghost' is enabled in lazarus.ini but no such mod is installed"), "missing mod reported");
    });

    scenario("graphics-settings: applies config, survives preset reset, re-applies after game meddles", [] {
        reset(false);
        configure("[loader]\nlog_level = debug\n[mods]\nenabled = graphics-settings\n", "normal");
        spit(lz() / "mods" / "graphics-settings" / "config.ini",
             "[display]\nwindow_mode = borderless\nwidth = 2560\nheight = 1440\n"
             "[performance]\ntarget_fps = 144\nvsync = 0\n"
             "[quality]\nquality_level = 2\nlod_bias = 1.5\nanisotropic = force\n");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.r("quality") == "2", "quality level " + run.r("quality"));
        // SetQualityLevel resets vsync+lod to the preset, mod must apply its values after
        expect(run.r("vsync") == "0", "vsync stays off after preset switch and game meddling: " + run.r("vsync"));
        expect(run.r("lod_x100") == "150", "lod bias " + run.r("lod_x100"));
        expect(run.r("target_frame_rate") == "144", "fps cap restored after meddling: " + run.r("target_frame_rate"));
        expect(run.r("aniso") == "2", "aniso force");
        expect(run.r("res_w") == "2560" && run.r("res_h") == "1440" && run.r("res_mode") == "1", "resolution");
        expect(run.has("quality levels: 0=Low, 1=Medium, 2=High (current 1)"), "quality names logged");
        expect(run.has("frame rate cap set to 144"), "fps read-back logged");
        expect(run.has("frame rate cap was changed to 30 by the game, putting it back to 144"), "drift detected");
        expect(run.has("vsync was changed to 1 by the game"), "vsync drift detected");
        expect(run.has("display: 2560x1440 borderless"), "display logged");
        dump_on_fail(run);
    });

    scenario("graphics-settings: empty config leaves the game alone", [] {
        reset(false);
        configure("[mods]\nenabled = graphics-settings\n", "normal");
        spit(lz() / "mods" / "graphics-settings" / "config.ini", "[performance]\ntarget_fps =\n");
        auto run = launch();
        expect(run.r("target_frame_rate") == "30", "game's own value untouched: " + run.r("target_frame_rate"));
        expect(run.r("quality") == "1", "quality untouched");
        expect(run.r("res_w") == "1920", "resolution untouched");
        expect(run.has("config.ini has nothing set"), "idle message");
    });

    scenario("graphics-settings: bad values warn, good values still apply", [] {
        reset(false);
        configure("[mods]\nenabled = graphics-settings\n", "normal");
        spit(lz() / "mods" / "graphics-settings" / "config.ini",
             "[performance]\ntarget_fps = 2\nvsync = 0\n[quality]\nquality_level = 7\n");
        auto run = launch();
        expect(run.r("vsync") == "0", "valid vsync applied");
        expect(run.r("quality") == "1", "nonexistent quality level ignored");
        expect(run.has("target_fps = '2' is out of range"), "bad fps warned");
        expect(run.has("quality_level 7 doesn't exist (game has 3 levels)"), "bad quality warned");
    });

    scenario("fps-counter: hooks real DXGI Present and counts frames exactly", [] {
        reset(false);
        configure("[loader]\nlog_level = debug\n[mods]\nenabled = fps-counter, graphics-settings\n", "normal");
        spit(lz() / "mods" / "graphics-settings" / "config.ini", "[performance]\ntarget_fps = 45\n");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.r("frames_rendered") == "60", "fake engine presented 60 frames: " + run.r("frames_rendered"));
        expect(run.has("present hook(s)"), "hooks installed");
        // 60 real presents; the DXGI_PRESENT_TEST call must not count
        expect(run.has("frames presented this session: 60"), "exact frame count");
        expect(run.has("startup complete: 2 mod(s) active"), "both mods active together");
        expect(run.r("target_frame_rate") == "45", "graphics-settings still applied alongside it");
        dump_on_fail(run);
    });

    scenario("dev-console: build pin override, autoexec routes commands, output mirrored to log", [] {
        reset(false);
        // dev-console is pinned to the real game build; the fake engine isn't it
        configure("[loader]\nignore_build_check = true\n[mods]\nenabled = dev-console, test-mod\n", "normal");
        spit(lz() / "mods" / "dev-console" / "config.ini",
             "[autoexec]\ncommands = echo autoexec says hi; test.echo \"a b\"; mods; nonsense\n"
             "after_scene = MainMenu\ndelay_frames = 2\n");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.has("is not marked compatible with game build"), "pin override warned");
        expect(run.has("level/cheat/inspect commands disabled"), "degrades without full il2cpp API");
        expect(run.has("autoexec: 'MainMenu' loaded, running in 2 frames"), "autoexec armed on scene");
        expect(run.has("[mod:dev-console] autoexec says hi"), "echo output");
        expect(run.has("[mod:dev-console] echo:a b"), "another mod's command via console");
        expect(run.has("[mod:dev-console] test-mod v1.0.0  active"), "loader 'mods' builtin");
        expect(run.has("[mod:dev-console] unknown command 'nonsense'"), "unknown command hint");
        expect(run.has("startup complete: 2 mod(s) active"), "both mods up");
        dump_on_fail(run);
    });

    scenario("missing lazarus.ini: defaults, nothing loaded", [] {
        reset(false);
        configure(kEnabled, "normal");
        fs::remove(lz() / "lazarus.ini");
        auto run = launch();
        expect(run.r("finished") == "1", "game finished");
        expect(run.r("value_in_game") == "1", "nothing enabled by default");
        expect(run.has("no config at"), "missing config warned");
    });

    std::printf("\n%s: %d failed assertion(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
