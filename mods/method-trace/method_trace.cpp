// method tracer. logs calls to whatever methods config.ini lists, with their
// args and return values, so a softlock shows up in lazarus.log as "last
// thing that happened before nothing happened".
//
// the trick: on x64 windows the first four int/pointer args ride in
// rcx/rdx/r8/r9, so one detour that takes four uintptr_t and passes them on
// works for any method whose args are all ints/bools/enums/references. floats
// and structs go elsewhere, so those methods get refused instead of mangled.

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "core/ini.h"
#include "lazarus/lazarus_api.h"
#include "trace_config.h"

namespace fs = std::filesystem;
using trace::Kind;

namespace {

const LazarusApi* api = nullptr;
LazarusMod* self = nullptr;
DWORD g_main_tid = 0;
std::atomic<bool> g_muted{false};

// il2cpp bits we need. the type/name ones are optional - without them we can
// still trace, we just can't type-check or print names
uint32_t (*method_get_flags)(void*, uint32_t*) = nullptr;
uint32_t (*method_get_param_count)(void*) = nullptr;
void* (*method_get_param)(void*, uint32_t) = nullptr;
void* (*method_get_return_type)(void*) = nullptr;
int (*type_get_type)(void*) = nullptr;
void* (*class_from_type)(void*) = nullptr;
bool (*class_is_enum)(void*) = nullptr;
bool (*class_is_valuetype)(void*) = nullptr;
void* (*object_get_class)(void*) = nullptr;
const char* (*class_get_name)(void*) = nullptr;
const wchar_t* (*string_chars)(void*) = nullptr;
int32_t (*string_length)(void*) = nullptr;
int32_t (*frame_count)() = nullptr;

template <typename T>
void want(T& fn, const char* name) {
    fn = reinterpret_cast<T>(api->il2cpp_export(name));
}

using Fwd = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

struct Slot {
    void* target = nullptr;
    Fwd orig = nullptr;
    std::string label;  // Class.Method
    bool is_static = false;
    std::vector<Kind> args;
    Kind ret = Kind::Void;
    std::atomic<long> calls{0};
};

constexpr int kSlots = 48;  // plenty for one investigation, bump if it ever isn't
Slot g_slots[kSlots];
int g_used = 0;

thread_local int t_depth = 0;

std::string fmt_string(void* s) {
    if (!s) return "null";
    if (!string_chars || !string_length) return "\"?\"";
    std::string out = "\"";
    const wchar_t* w = string_chars(s);
    for (int32_t i = 0, n = string_length(s); i < n && i < 80; ++i) out += (w[i] < 128) ? (char)w[i] : '?';
    return out + "\"";
}

std::string fmt(Kind k, uintptr_t v) {
    char buf[64];
    switch (k) {
        case Kind::Bool: return (v & 0xff) ? "true" : "false";
        case Kind::Int32: case Kind::Enum: std::snprintf(buf, sizeof buf, "%d", (int32_t)v); return buf;
        case Kind::Int64: std::snprintf(buf, sizeof buf, "%lld", (long long)v); return buf;
        case Kind::String: return fmt_string((void*)v);
        case Kind::Object:
            if (!v) return "null";
            // class name only, calling back into managed code from in here
            // (get_name etc) is asking for re-entrancy fun
            if (object_get_class && class_get_name) return std::string("<") + class_get_name(object_get_class((void*)v)) + ">";
            std::snprintf(buf, sizeof buf, "%p", (void*)v);
            return buf;
        default: return "?";
    }
}

// first 25 calls of each, then every 500th. some of these (CanBossDuelQTELoop
// probably) get polled every frame and would drown everything else
bool should_log(long n) { return n <= 25 || n % 500 == 0; }

void on_call(Slot& s, const uintptr_t (&regs)[4], long n) {
    std::string line(t_depth * 2, ' ');
    line += s.label + "(";
    int r = s.is_static ? 0 : 1;
    for (size_t i = 0; i < s.args.size(); ++i) line += (i ? ", " : "") + fmt(s.args[i], regs[r + i]);
    line += ")";
    if (n > 25) line += "  [call #" + std::to_string(n) + "]";
    if (frame_count && GetCurrentThreadId() == g_main_tid) line += "  f" + std::to_string(frame_count());
    else if (GetCurrentThreadId() != g_main_tid) line += "  (off main thread)";
    api->log(self, LAZARUS_LOG_INFO, "%s", line.c_str());
}

template <int N>
uintptr_t detour(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    Slot& s = g_slots[N];
    long n = ++s.calls;
    bool log = !g_muted.load() && should_log(n);
    if (log) {
        uintptr_t regs[4] = {a, b, c, d};
        on_call(s, regs, n);
    }
    ++t_depth;
    uintptr_t r = s.orig(a, b, c, d);
    --t_depth;
    if (log && s.ret != Kind::Void && s.ret != Kind::Object)
        api->log(self, LAZARUS_LOG_INFO, "%*s%s -> %s", t_depth * 2, "", s.label.c_str(), fmt(s.ret, r).c_str());
    return r;
}

template <size_t... I>
constexpr std::array<void*, sizeof...(I)> make_detours(std::index_sequence<I...>) {
    return {reinterpret_cast<void*>(&detour<(int)I>)...};
}
const auto kDetours = make_detours(std::make_index_sequence<kSlots>{});

Kind kind_of_type(void* type) {
    if (!type || !type_get_type) return Kind::Unsupported;
    int t = type_get_type(type);
    void* k = class_from_type ? class_from_type(type) : nullptr;
    bool vt = k && class_is_valuetype && class_is_valuetype(k);
    bool en = k && class_is_enum && class_is_enum(k);
    return trace::classify(t, vt, en);
}

// returns "" on success, otherwise why not
std::string add_target(const trace::Target& t) {
    if (g_used >= kSlots) return "all " + std::to_string(kSlots) + " trace slots used";
    auto dot = t.klass.rfind('.');
    std::string ns = dot == std::string::npos ? "" : t.klass.substr(0, dot);
    std::string name = dot == std::string::npos ? t.klass : t.klass.substr(dot + 1);
    void* mi = api->find_method(t.assembly.c_str(), ns.c_str(), name.c_str(), t.method.c_str(), t.argc);
    if (!mi) return "not found";
    void* code = api->method_pointer(mi);
    for (int i = 0; i < g_used; ++i)
        if (g_slots[i].target == code) return "already traced (as " + g_slots[i].label + ")";

    Slot& s = g_slots[g_used];
    s.label = name + "." + t.method;
    uint32_t iflags = 0;
    s.is_static = method_get_flags && (method_get_flags(mi, &iflags) & 0x10);
    if (!trace::fits_registers(s.is_static, t.argc)) return "too many args for the register forwarder";
    s.args.clear();
    if (method_get_param && type_get_type) {
        for (int i = 0; i < t.argc; ++i) {
            Kind k = kind_of_type(method_get_param(mi, (uint32_t)i));
            if (k == Kind::Unsupported) return "arg " + std::to_string(i) + " is a float/struct, can't forward it safely";
            s.args.push_back(k);
        }
        s.ret = kind_of_type(method_get_return_type ? method_get_return_type(mi) : nullptr);
        if (s.ret == Kind::Unsupported) return "returns a float/struct, can't forward it safely";
    } else {
        // no type info: we can't prove it's safe, so don't
        return "this build doesn't export the type info we need";
    }
    if (api->hook_create(self, code, kDetours[g_used], reinterpret_cast<void**>(&s.orig)) != LAZARUS_OK)
        return "hook failed";
    s.target = code;
    s.calls = 0;
    ++g_used;
    return "";
}

void load_targets() {
    lz::Ini ini;
    std::string err;
    if (!lz::Ini::load(fs::u8path(api->mod_dir(self)) / "config.ini", ini, err)) return;
    int ok = 0, bad = 0;
    for (auto& key : ini.keys("methods")) {
        trace::Target t;
        std::string why;
        std::string line = ini.get_or("methods", key, "");
        if (line.empty()) continue;
        if (!trace::parse_target(line, t, why)) {
            api->log(self, LAZARUS_LOG_WARN, "config.ini [methods] %s: %s", key.c_str(), why.c_str());
            ++bad;
            continue;
        }
        why = add_target(t);
        if (why.empty()) {
            ++ok;
            api->log(self, LAZARUS_LOG_DEBUG, "tracing %s.%s/%d", t.klass.c_str(), t.method.c_str(), t.argc);
        } else {
            ++bad;
            api->log(self, LAZARUS_LOG_WARN, "can't trace %s.%s/%d: %s", t.klass.c_str(), t.method.c_str(), t.argc,
                     why.c_str());
        }
    }
    api->log(self, LAZARUS_LOG_INFO, "tracing %d method(s)%s", ok, bad ? (", " + std::to_string(bad) + " skipped").c_str() : "");
}

// ---- console

void cmd_list(void*, int, const char**, LazarusPrintFn print, void* ctx) {
    for (int i = 0; i < g_used; ++i) {
        std::string l = g_slots[i].label + "  calls: " + std::to_string(g_slots[i].calls.load());
        print(ctx, l.c_str());
    }
    if (!g_used) print(ctx, "nothing traced");
    if (g_muted) print(ctx, "(muted - trace.mute again to resume)");
}

void cmd_add(void*, int argc, const char** argv, LazarusPrintFn print, void* ctx) {
    std::string line;
    for (int i = 1; i < argc; ++i) line += std::string(i > 1 ? " " : "") + argv[i];
    trace::Target t;
    std::string why;
    if (!trace::parse_target(line, t, why)) {
        print(ctx, ("usage: trace.add Assembly.dll | Ns.Class | Method | argc  (" + why + ")").c_str());
        return;
    }
    why = add_target(t);
    print(ctx, why.empty() ? ("now tracing " + t.klass + "." + t.method).c_str() : ("can't: " + why).c_str());
}

void cmd_mute(void*, int, const char**, LazarusPrintFn print, void* ctx) {
    g_muted = !g_muted;
    print(ctx, g_muted ? "trace logging muted (counts still go up)" : "trace logging back on");
}

}  // namespace

extern "C" __declspec(dllexport) int lazarus_mod_init(LazarusMod* m, const LazarusApi* a) {
    api = a;
    self = m;
    if (a->api_version < 2 || a->struct_size < sizeof(LazarusApi)) return 1;
    g_main_tid = GetCurrentThreadId();
    want(method_get_flags, "il2cpp_method_get_flags");
    want(method_get_param_count, "il2cpp_method_get_param_count");
    want(method_get_param, "il2cpp_method_get_param");
    want(method_get_return_type, "il2cpp_method_get_return_type");
    want(type_get_type, "il2cpp_type_get_type");
    want(class_from_type, "il2cpp_class_from_type");
    want(class_is_enum, "il2cpp_class_is_enum");
    want(class_is_valuetype, "il2cpp_class_is_valuetype");
    want(object_get_class, "il2cpp_object_get_class");
    want(class_get_name, "il2cpp_class_get_name");
    want(string_chars, "il2cpp_string_chars");
    want(string_length, "il2cpp_string_length");
    frame_count = reinterpret_cast<int32_t (*)()>(a->resolve_icall("UnityEngine.Time::get_frameCount"));

    load_targets();
    a->register_command(m, "trace.list", "traced methods and call counts", &cmd_list, nullptr);
    a->register_command(m, "trace.add", "trace another method: trace.add Asm.dll | Ns.Class | Method | argc", &cmd_add, nullptr);
    a->register_command(m, "trace.mute", "toggle trace logging", &cmd_mute, nullptr);
    return 0;
}
