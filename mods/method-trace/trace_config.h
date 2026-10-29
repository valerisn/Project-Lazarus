#pragma once
// the bits of the tracer that don't need the game, so they can be tested

#include <string>

namespace trace {

// "Lucille.BossSystem.dll | Lucille.BossSystem.BossSystem | StartBossPart | 0"
struct Target {
    std::string assembly;
    std::string klass;  // Ns.Class
    std::string method;
    int argc = 0;
};

bool parse_target(const std::string& line, Target& out, std::string& err);

// what an argument/return looks like to us. Unsupported = float, double or a
// real struct: those don't travel in the integer registers, so the generic
// forwarder would mangle them
enum class Kind { Unsupported, Void, Bool, Int32, Int64, Enum, String, Object };

// type_code is IL2CPP_TYPE_* (il2cpp_type_get_type)
Kind classify(int type_code, bool is_valuetype, bool is_enum);

// this + args + the hidden MethodInfo* all have to fit in rcx/rdx/r8/r9
inline bool fits_registers(bool is_static, int argc) { return (is_static ? 0 : 1) + argc + 1 <= 4; }

const char* kind_name(Kind k);

}  // namespace trace
