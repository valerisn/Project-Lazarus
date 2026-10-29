#pragma once
// just enough il2cpp to poke at game objects. main thread only

#include <cstdint>
#include <string>
#include <vector>

#include "lazarus/lazarus_api.h"

namespace il2 {

using Obj = void;    // Il2CppObject*
using Klass = void;  // Il2CppClass*
using Method = void; // const MethodInfo*

bool init(const LazarusApi* api);  // false = some export is missing
const char* missing();             // ...which one

Klass* find_class(const char* full_name, const char* assembly = nullptr);  // "Ns.Class"
Method* find_method(Klass* k, const char* name, int argc);                 // walks base classes
Obj* type_object(Klass* k);                                                // System.Type for k

// managed exceptions come back in err instead of blowing up. value types come back boxed
Obj* invoke(Method* m, void* obj, void** params, std::string& err);

std::string to_utf8(Obj* il2cpp_string);
Obj* new_string(const std::string& utf8);
Obj* static_field(Klass* k, const char* name);  // reference types only
Klass* class_of(Obj* o);
std::string class_name(Klass* k);  // "Ns.Class"
std::string object_name(Obj* unity_object);  // UnityEngine.Object.name

uint32_t array_length(Obj* arr);
Obj** array_items(Obj* arr);
bool unbox_bool(Obj* boxed);
int32_t unbox_int(Obj* boxed);

struct MethodDesc {
    std::string name;
    int argc;
    bool is_static;
};
std::vector<MethodDesc> methods_of(Klass* k);
std::vector<std::string> fields_of(Klass* k);

// Resources.FindObjectsOfTypeAll - assets too, inactive too
std::vector<Obj*> find_all(Klass* k, std::string& err);

// Object.FindObjectsOfType(type, true) - scene objects only
std::vector<Obj*> find_in_scenes(Klass* k, std::string& err);

// "Class on 'GameObject'" for every Behaviour in loaded scenes. this is how we
// found out the logos were VideoPlayers
std::vector<std::string> census(std::string& err);

// every instance field as "name = value", own class first then base classes,
// stopping at unity's own types. false if the type info exports are missing
bool describe(Obj* o, std::vector<std::string>& out);

// call an instance method by name, args parsed from text into whatever the
// method actually takes (bool/int/enum/string). returns false + err if it
// can't, otherwise result is a printable summary of the return value
bool call_with_args(Obj* o, const std::string& method, const std::vector<std::string>& args, std::string& result,
                    std::string& err);

}  // namespace il2
