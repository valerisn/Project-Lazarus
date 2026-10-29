#include "il2cpp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace il2 {
namespace {

const LazarusApi* g_api = nullptr;
const char* g_missing = nullptr;

void* (*domain_get)();
void** (*domain_get_assemblies)(void*, size_t*);
void* (*assembly_get_image)(void*);
const char* (*image_get_name)(void*);
void* (*class_from_name)(void*, const char*, const char*);
void* (*class_get_method_from_name)(void*, const char*, int);
void* (*class_get_parent)(void*);
void* (*class_get_type)(void*);
void* (*type_get_object)(void*);
const char* (*class_get_name)(void*);
const char* (*class_get_namespace)(void*);
void* (*class_get_methods)(void*, void**);
void* (*class_get_fields)(void*, void**);
void* (*class_get_field_from_name)(void*, const char*);
void (*field_static_get_value)(void*, void*);
const char* (*field_get_name)(void*);
int (*field_get_flags)(void*);
const char* (*method_get_name)(void*);
uint32_t (*method_get_param_count)(void*);
uint32_t (*method_get_flags)(void*, uint32_t*);
void* (*runtime_invoke)(void*, void*, void**, void**);
void (*format_exception)(void*, char*, int);
void* (*object_get_class)(void*);
void* (*object_unbox)(void*);
void* (*string_new)(const char*);
const wchar_t* (*string_chars)(void*);
int32_t (*string_length)(void*);
uint32_t (*array_len)(void*);
// optional, only inspect/invoke need these
void* (*field_get_type)(void*);
size_t (*field_get_offset)(void*);
void* (*method_get_param)(void*, uint32_t);
int (*type_get_type)(void*);
void* (*class_from_type)(void*);
bool (*class_is_enum)(void*);
bool (*class_is_valuetype)(void*);

template <typename T>
void want(T& fn, const char* name) {
    fn = reinterpret_cast<T>(g_api->il2cpp_export(name));
}

template <typename T>
void need(T& fn, const char* name) {
    fn = reinterpret_cast<T>(g_api->il2cpp_export(name));
    if (!fn && !g_missing) g_missing = name;
}

}  // namespace

bool init(const LazarusApi* api) {
    g_api = api;
    need(domain_get, "il2cpp_domain_get");
    need(domain_get_assemblies, "il2cpp_domain_get_assemblies");
    need(assembly_get_image, "il2cpp_assembly_get_image");
    need(image_get_name, "il2cpp_image_get_name");
    need(class_from_name, "il2cpp_class_from_name");
    need(class_get_method_from_name, "il2cpp_class_get_method_from_name");
    need(class_get_parent, "il2cpp_class_get_parent");
    need(class_get_type, "il2cpp_class_get_type");
    need(type_get_object, "il2cpp_type_get_object");
    need(class_get_name, "il2cpp_class_get_name");
    need(class_get_namespace, "il2cpp_class_get_namespace");
    need(class_get_methods, "il2cpp_class_get_methods");
    need(class_get_fields, "il2cpp_class_get_fields");
    need(class_get_field_from_name, "il2cpp_class_get_field_from_name");
    need(field_static_get_value, "il2cpp_field_static_get_value");
    need(field_get_name, "il2cpp_field_get_name");
    need(field_get_flags, "il2cpp_field_get_flags");
    need(method_get_name, "il2cpp_method_get_name");
    need(method_get_param_count, "il2cpp_method_get_param_count");
    need(method_get_flags, "il2cpp_method_get_flags");
    need(runtime_invoke, "il2cpp_runtime_invoke");
    need(format_exception, "il2cpp_format_exception");
    need(object_get_class, "il2cpp_object_get_class");
    need(object_unbox, "il2cpp_object_unbox");
    need(string_new, "il2cpp_string_new");
    need(string_chars, "il2cpp_string_chars");
    need(string_length, "il2cpp_string_length");
    need(array_len, "il2cpp_array_length");
    want(field_get_type, "il2cpp_field_get_type");
    want(field_get_offset, "il2cpp_field_get_offset");
    want(method_get_param, "il2cpp_method_get_param");
    want(type_get_type, "il2cpp_type_get_type");
    want(class_from_type, "il2cpp_class_from_type");
    want(class_is_enum, "il2cpp_class_is_enum");
    want(class_is_valuetype, "il2cpp_class_is_valuetype");
    return g_missing == nullptr;
}

const char* missing() { return g_missing ? g_missing : ""; }

Klass* find_class(const char* full_name, const char* assembly) {
    std::string full(full_name ? full_name : "");
    auto dot = full.rfind('.');
    std::string ns = dot == std::string::npos ? "" : full.substr(0, dot);
    std::string name = dot == std::string::npos ? full : full.substr(dot + 1);
    size_t n = 0;
    void** asms = domain_get_assemblies(domain_get(), &n);
    for (size_t i = 0; i < n; ++i) {
        void* img = assembly_get_image(asms[i]);
        if (!img) continue;
        if (assembly && *assembly && _stricmp(image_get_name(img), assembly) != 0) continue;
        if (void* k = class_from_name(img, ns.c_str(), name.c_str())) return k;
    }
    return nullptr;
}

Method* find_method(Klass* k, const char* name, int argc) {
    // don't trust the export to check base classes
    for (; k; k = class_get_parent(k))
        if (void* m = class_get_method_from_name(k, name, argc)) return m;
    return nullptr;
}

Obj* type_object(Klass* k) { return k ? type_get_object(class_get_type(k)) : nullptr; }

Obj* invoke(Method* m, void* obj, void** params, std::string& err) {
    if (!m) {
        err = "no such method";
        return nullptr;
    }
    void* exc = nullptr;
    void* r = runtime_invoke(m, obj, params, &exc);
    if (exc) {
        char buf[1024] = {};
        format_exception(exc, buf, sizeof buf);
        err = buf[0] ? buf : "managed exception";
        return nullptr;
    }
    return r;
}

std::string to_utf8(Obj* s) {
    if (!s) return "";
    const wchar_t* w = string_chars(s);
    int32_t len = string_length(s);
    std::string out;
    for (int32_t i = 0; i < len; ++i) {
        wchar_t c = w[i];
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
        else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
}

Obj* new_string(const std::string& s) { return string_new(s.c_str()); }

Obj* static_field(Klass* k, const char* name) {
    for (; k; k = class_get_parent(k)) {
        void* f = class_get_field_from_name(k, name);
        if (!f) continue;
        void* out = nullptr;
        field_static_get_value(f, &out);
        return out;
    }
    return nullptr;
}

Klass* class_of(Obj* o) { return o ? object_get_class(o) : nullptr; }

std::string class_name(Klass* k) {
    if (!k) return "?";
    std::string ns = class_get_namespace(k);
    return ns.empty() ? class_get_name(k) : ns + "." + class_get_name(k);
}

std::string object_name(Obj* o) {
    static Method* get_name = nullptr;
    if (!get_name) get_name = find_method(find_class("UnityEngine.Object", "UnityEngine.CoreModule.dll"), "get_name", 0);
    std::string err;
    Obj* s = invoke(get_name, o, nullptr, err);
    return s ? to_utf8(s) : "<" + err + ">";
}

uint32_t array_length(Obj* arr) { return arr ? array_len(arr) : 0; }
// Il2CppArray: 16 byte header, bounds ptr, max_length, items
Obj** array_items(Obj* arr) { return reinterpret_cast<Obj**>(static_cast<char*>(arr) + 32); }
bool unbox_bool(Obj* b) { return b && *static_cast<bool*>(object_unbox(b)); }
int32_t unbox_int(Obj* b) { return b ? *static_cast<int32_t*>(object_unbox(b)) : 0; }

std::vector<MethodDesc> methods_of(Klass* k) {
    std::vector<MethodDesc> out;
    void* iter = nullptr;
    while (void* m = class_get_methods(k, &iter)) {
        uint32_t iflags = 0;
        uint32_t flags = method_get_flags(m, &iflags);
        out.push_back({method_get_name(m), (int)method_get_param_count(m), (flags & 0x10) != 0});
    }
    return out;
}

std::vector<std::string> fields_of(Klass* k) {
    std::vector<std::string> out;
    void* iter = nullptr;
    while (void* f = class_get_fields(k, &iter))
        out.push_back(std::string((field_get_flags(f) & 0x10) ? "static " : "") + field_get_name(f));
    return out;
}

std::vector<Obj*> find_in_scenes(Klass* k, std::string& err) {
    std::vector<Obj*> out;
    static Method* fot = nullptr;
    if (!fot) fot = find_method(find_class("UnityEngine.Object", "UnityEngine.CoreModule.dll"), "FindObjectsOfType", 2);
    if (!fot) {
        err = "Object.FindObjectsOfType(type, bool) not found";
        return out;
    }
    bool include_inactive = true;
    void* args[] = {type_object(k), &include_inactive};
    Obj* arr = invoke(fot, nullptr, args, err);
    if (!arr) return out;
    uint32_t n = array_length(arr);
    Obj** items = array_items(arr);
    for (uint32_t i = 0; i < n; ++i) out.push_back(items[i]);
    return out;
}

std::vector<std::string> census(std::string& err) {
    std::vector<std::string> lines;
    Klass* behaviour = find_class("UnityEngine.Behaviour", "UnityEngine.CoreModule.dll");
    if (!behaviour) {
        err = "UnityEngine.Behaviour not found";
        return lines;
    }
    // linear scan is fine, these scenes have dozens of components not thousands
    std::vector<std::pair<std::string, int>> seen;
    for (Obj* o : find_in_scenes(behaviour, err)) {
        std::string key = class_name(class_of(o)) + " on '" + object_name(o) + "'";
        bool found = false;
        for (auto& s : seen)
            if (s.first == key) {
                ++s.second;
                found = true;
                break;
            }
        if (!found) seen.push_back({key, 1});
    }
    for (auto& s : seen) lines.push_back(s.second > 1 ? s.first + " x" + std::to_string(s.second) : s.first);
    return lines;
}

namespace {

// IL2CPP_TYPE_* codes we care about
enum : int { T_BOOL = 0x02, T_CHAR = 0x03, T_I1 = 0x04, T_U1, T_I2, T_U2, T_I4, T_U4, T_I8, T_U8, T_R4, T_R8,
             T_STRING = 0x0e, T_VALUETYPE = 0x11, T_CLASS = 0x12, T_GENERICINST = 0x15, T_OBJECT = 0x1c,
             T_SZARRAY = 0x1d };

std::string fmt_field(void* type, const char* p) {
    int t = type_get_type(type);
    char buf[64];
    switch (t) {
        case T_BOOL: return *(const bool*)p ? "true" : "false";
        case T_I1: return std::to_string(*(const int8_t*)p);
        case T_U1: return std::to_string(*(const uint8_t*)p);
        case T_I2: return std::to_string(*(const int16_t*)p);
        case T_U2: case T_CHAR: return std::to_string(*(const uint16_t*)p);
        case T_I4: return std::to_string(*(const int32_t*)p);
        case T_U4: return std::to_string(*(const uint32_t*)p);
        case T_I8: return std::to_string(*(const int64_t*)p);
        case T_U8: return std::to_string(*(const uint64_t*)p);
        case T_R4: std::snprintf(buf, sizeof buf, "%g", *(const float*)p); return buf;
        case T_R8: std::snprintf(buf, sizeof buf, "%g", *(const double*)p); return buf;
        case T_STRING: {
            void* str = *(void* const*)p;
            return str ? "\"" + to_utf8(str) + "\"" : "null";
        }
        case T_VALUETYPE: {
            void* k = class_from_type ? class_from_type(type) : nullptr;
            // enums are an int32 underneath in this game. anything else is a
            // real struct, not worth decoding blind
            if (k && class_is_enum && class_is_enum(k)) return std::to_string(*(const int32_t*)p) + " (" + class_get_name(k) + ")";
            return std::string("<struct ") + (k ? class_get_name(k) : "?") + ">";
        }
        case T_CLASS: case T_OBJECT: case T_SZARRAY: case T_GENERICINST: {
            void* k = class_from_type ? class_from_type(type) : nullptr;
            if (t == T_GENERICINST && k && class_is_valuetype && class_is_valuetype(k)) return "<struct>";
            void* ref = *(void* const*)p;
            if (!ref) return "null";
            return "<" + class_name(class_of(ref)) + ">";
        }
        default: return "?";
    }
}

}  // namespace

bool describe(Obj* o, std::vector<std::string>& out) {
    if (!o || !field_get_type || !field_get_offset || !type_get_type) return false;
    for (Klass* k = class_of(o); k; k = class_get_parent(k)) {
        std::string ns = class_get_namespace(k);
        if (ns.rfind("UnityEngine", 0) == 0 || ns == "System") break;  // their fields are noise
        void* iter = nullptr;
        while (void* f = class_get_fields(k, &iter)) {
            if (field_get_flags(f) & 0x10) continue;  // statics live elsewhere
            out.push_back(std::string(field_get_name(f)) + " = " +
                          fmt_field(field_get_type(f), static_cast<const char*>(o) + field_get_offset(f)));
        }
    }
    return true;
}

bool call_with_args(Obj* o, const std::string& method, const std::vector<std::string>& args, std::string& result,
                    std::string& err) {
    Method* m = find_method(class_of(o), method.c_str(), (int)args.size());
    if (!m) {
        err = "no " + method + " with " + std::to_string(args.size()) + " arg(s) on " + class_name(class_of(o));
        return false;
    }
    if (!args.empty() && (!method_get_param || !type_get_type)) {
        err = "this build doesn't export param type info";
        return false;
    }
    // storage has to outlive the invoke, runtime_invoke takes pointers to the values
    std::vector<int64_t> nums(args.size());
    std::vector<void*> params(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        void* type = method_get_param(m, (uint32_t)i);
        int t = type_get_type(type);
        const std::string& a = args[i];
        if (t == T_BOOL) {
            if (a != "true" && a != "false" && a != "1" && a != "0") {
                err = "arg " + std::to_string(i + 1) + " wants true/false";
                return false;
            }
            nums[i] = (a == "true" || a == "1") ? 1 : 0;
            params[i] = &nums[i];
        } else if ((t >= T_I1 && t <= T_U8) || t == T_CHAR ||
                   (t == T_VALUETYPE && class_from_type && class_is_enum && class_is_enum(class_from_type(type)))) {
            char* end = nullptr;
            nums[i] = std::strtoll(a.c_str(), &end, 10);
            if (a.empty() || *end) {
                err = "arg " + std::to_string(i + 1) + " wants a number";
                return false;
            }
            params[i] = &nums[i];  // little endian, so an int32 reads the low half. fine
        } else if (t == T_STRING) {
            params[i] = new_string(a);
        } else if (a == "null" && (t == T_CLASS || t == T_OBJECT || t == T_SZARRAY || t == T_GENERICINST)) {
            params[i] = nullptr;
        } else {
            err = "arg " + std::to_string(i + 1) + " is a type we can't make from text (only null for objects)";
            return false;
        }
    }
    Obj* r = invoke(m, o, params.empty() ? nullptr : params.data(), err);
    if (!err.empty()) return false;
    result = r ? "returned <" + class_name(class_of(r)) + ">" : "done";
    return true;
}

std::vector<Obj*> find_all(Klass* k, std::string& err) {
    std::vector<Obj*> out;
    static Method* fota = nullptr;
    if (!fota) fota = find_method(find_class("UnityEngine.Resources", "UnityEngine.CoreModule.dll"), "FindObjectsOfTypeAll", 1);
    if (!fota) {
        err = "Resources.FindObjectsOfTypeAll not found";
        return out;
    }
    void* type = type_object(k);
    void* args[] = {type};
    Obj* arr = invoke(fota, nullptr, args, err);
    if (!arr) return out;
    uint32_t n = array_length(arr);
    Obj** items = array_items(arr);
    for (uint32_t i = 0; i < n; ++i) out.push_back(items[i]);
    return out;
}

}  // namespace il2
