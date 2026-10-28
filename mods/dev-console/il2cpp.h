#pragma once
// Thin wrapper over the il2cpp C exports, enough for poking at game objects
// from console commands. Everything here is main-thread only.

#include <cstdint>
#include <string>
#include <vector>

#include "lazarus/lazarus_api.h"

namespace il2 {

using Obj = void;    // Il2CppObject*
using Klass = void;  // Il2CppClass*
using Method = void; // const MethodInfo*

bool init(const LazarusApi* api);  // false if a required export is missing
const char* missing();             // name of the first missing export

Klass* find_class(const char* full_name, const char* assembly = nullptr);  // "Ns.Sub.Class"
Method* find_method(Klass* k, const char* name, int argc);                 // walks base classes
Obj* type_object(Klass* k);                                                // System.Type for k

// runtime_invoke with managed exceptions turned into `err`. returns the
// (boxed for value types) result or null.
Obj* invoke(Method* m, void* obj, void** params, std::string& err);

std::string to_utf8(Obj* il2cpp_string);
Obj* new_string(const std::string& utf8);
Obj* static_field(Klass* k, const char* name);  // reference-type static fields only
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

// UnityEngine.Resources.FindObjectsOfTypeAll(type): assets + scene objects, active or not
std::vector<Obj*> find_all(Klass* k, std::string& err);

// UnityEngine.Object.FindObjectsOfType(type, includeInactive=true): scene objects only
std::vector<Obj*> find_in_scenes(Klass* k, std::string& err);

// "Class on 'GameObject'" for every Behaviour in loaded scenes, deduped with counts.
// that's how we find out what actually drives a scene (timeline? video? script?)
std::vector<std::string> census(std::string& err);

}  // namespace il2
