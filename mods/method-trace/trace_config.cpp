#include "trace_config.h"

#include <cerrno>
#include <cstdlib>
#include <vector>

#include "core/ini.h"

namespace trace {

bool parse_target(const std::string& line, Target& out, std::string& err) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t bar = line.find('|', start);
        parts.push_back(lz::trim(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start)));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    if (parts.size() != 4) {
        err = "expected 'Assembly.dll | Ns.Class | Method | argc'";
        return false;
    }
    out.assembly = parts[0];
    out.klass = parts[1];
    out.method = parts[2];
    if (out.assembly.empty() || out.klass.empty() || out.method.empty()) {
        err = "assembly, class and method can't be empty";
        return false;
    }
    char* end = nullptr;
    errno = 0;
    long n = std::strtol(parts[3].c_str(), &end, 10);
    if (parts[3].empty() || *end || errno || n < 0 || n > 3) {
        err = "argc must be 0-3, got '" + parts[3] + "'";
        return false;
    }
    out.argc = (int)n;
    return true;
}

Kind classify(int t, bool is_valuetype, bool is_enum) {
    switch (t) {
        case 0x01: return Kind::Void;
        case 0x02: return Kind::Bool;
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09:
            return Kind::Int32;  // char, i1..u4 all come through as one register
        case 0x0a: case 0x0b: case 0x18: case 0x19:
            return Kind::Int64;
        case 0x0e: return Kind::String;
        case 0x12: case 0x1c: case 0x1d: case 0x14:
            return Kind::Object;
        case 0x11: return is_enum ? Kind::Enum : Kind::Unsupported;
        case 0x15: return is_valuetype ? Kind::Unsupported : Kind::Object;  // List<T> yes, Nullable<T> no
        default: return Kind::Unsupported;  // floats, ptrs, byref, generics params...
    }
}

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Unsupported: return "unsupported";
        case Kind::Void: return "void";
        case Kind::Bool: return "bool";
        case Kind::Int32: return "int";
        case Kind::Int64: return "long";
        case Kind::Enum: return "enum";
        case Kind::String: return "string";
        case Kind::Object: return "object";
    }
    return "?";
}

}  // namespace trace
