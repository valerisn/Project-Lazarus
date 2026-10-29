// console commands. the loader runs these on the main thread, so touching
// il2cpp/unity from here is fine.
//
// the devs left their whole cheat system in the release build
// (Lucille.LucilleCheats) and every level is a SceneCollection with a Load()
// method, so most of this file just calls into those

#include "commands.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "cheat_policy.h"
#include "il2cpp.h"

namespace con {
namespace {

const LazarusApi* api = nullptr;
LazarusMod* self = nullptr;

struct Out {
    LazarusPrintFn fn;
    void* ctx;
    void operator()(const std::string& s) const { fn(ctx, s.c_str()); }
};

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

using Handler = std::function<void(const std::vector<std::string>&, const Out&)>;
std::vector<Handler> g_handlers;  // the index rides in the api's user pointer

void dispatch(void* user, int argc, const char** argv, LazarusPrintFn p, void* c) {
    std::vector<std::string> args(argv, argv + argc);
    g_handlers[(size_t)user](args, Out{p, c});
}

void reg(const char* name, const char* help, Handler h) {
    g_handlers.push_back(std::move(h));
    api->register_command(self, name, help, &dispatch, (void*)(g_handlers.size() - 1));
}

// ------------------------------------------------------------- game types

il2::Klass* scene_collection_class() {
    static il2::Klass* k = nullptr;
    if (!k) k = il2::find_class("Flux.LoadSceneManager.SceneCollection", "FluxSceneManager.dll");
    return k;
}

il2::Klass* cheats_class() {
    static il2::Klass* k = nullptr;
    if (!k) k = il2::find_class("Lucille.LucilleCheats", "Lucille.Cheats.dll");
    return k;
}

il2::Obj* cheats_instance(const Out& out) {
    il2::Klass* k = cheats_class();
    if (!k) {
        out("Lucille.LucilleCheats class not found in this build");
        return nullptr;
    }
    il2::Obj* inst = il2::static_field(k, "instance");
    if (!inst) {
        // no idea yet if the release build ever spawns this. live run will tell
        std::string err;
        auto all = il2::find_all(k, err);
        if (!all.empty()) inst = all[0];
    }
    if (!inst) out("LucilleCheats isn't alive right now (not spawned in this scene/build). try again in a level.");
    return inst;
}

struct Collection {
    il2::Obj* obj;
    std::string name;
};

std::vector<Collection> collections(const Out& out) {
    std::vector<Collection> list;
    il2::Klass* k = scene_collection_class();
    if (!k) {
        out("SceneCollection class not found");
        return list;
    }
    std::string err;
    for (il2::Obj* o : il2::find_all(k, err)) list.push_back({o, il2::object_name(o)});
    if (!err.empty()) out("FindObjectsOfTypeAll failed: " + err);
    return list;
}

// the game's own master list of levels (MomentSystem.GlobalMoments). unlike
// FindObjectsOfTypeAll this doesn't care what happens to be loaded right now,
// which is what makes loading from the main menu possible
std::vector<Collection> global_levels(const Out& out) {
    std::vector<Collection> list;
    il2::Klass* ms = il2::find_class("Lucille.Moment.MomentSystem", "Lucille.MomentSystem.dll");
    if (!ms) return list;
    std::string err;
    il2::Obj* g = il2::invoke(il2::find_method(ms, "get_GlobalMoments", 0), nullptr, nullptr, err);
    if (!g) {
        if (!err.empty()) out("GlobalMoments threw: " + err);
        return list;
    }
    auto items = il2::elements(g);
    if (items.empty()) {
        // not a list itself? then it's a wrapper object, look for the list inside
        for (const char* f : {"_globalMoments", "globalMoments", "_items", "items", "moments", "_moments"})
            if (il2::Obj* inner = il2::ref_field(g, f)) {
                items = il2::elements(inner);
                if (!items.empty()) break;
            }
    }
    for (il2::Obj* it : items)
        if (il2::Obj* sc = il2::ref_field(it, "SceneCollection")) list.push_back({sc, il2::object_name(sc)});
    if (list.empty()) out("couldn't read the level list (GlobalMoments is a " + il2::class_name(il2::class_of(g)) + ")");
    return list;
}

std::vector<Collection*> match(std::vector<Collection>& list, const std::string& q) {
    std::vector<Collection*> hits;
    std::string lq = lower(q);
    for (auto& c : list)
        if (lower(c.name) == lq) return {&c};
    for (auto& c : list)
        if (lower(c.name).find(lq) != std::string::npos) hits.push_back(&c);
    return hits;
}

const char* kCheatItems[] = {"Bottle", "FlashBang", "HandmadeGrenade", "GrenadeMilitary", "Medicine", "StressPills",
                             "SurgeryMedicine", "Bat", "FireAxe", "Sledgehammer", "Machete", "Katana", "Crowbar",
                             "Colt", "Rifle", "Crossbow", "Pistol", "Sniper", "Shotgun"};

il2::Obj* inventory_manager() {
    il2::Klass* k = il2::find_class("Lucille.InventorySystem.InventoryManager", "Lucille.InventorySystem.dll");
    if (!k) return nullptr;
    if (il2::Obj* o = il2::static_field(k, "instance")) return o;
    std::string err;
    auto all = il2::find_in_scenes(k, err);
    return all.empty() ? nullptr : all[0];
}

// call a cheats method (0 or 1 int arg), then print the matching getter if given
bool cheat_call(const Out& out, const char* method, const char* getter = nullptr, int* int_arg = nullptr) {
    il2::Obj* inst = cheats_instance(out);
    if (!inst) return false;
    il2::Method* m = il2::find_method(il2::class_of(inst), method, int_arg ? 1 : 0);
    if (!m) {
        out(std::string("LucilleCheats has no method ") + method);
        return false;
    }
    std::string err;
    void* args[] = {int_arg};
    il2::invoke(m, inst, int_arg ? args : nullptr, err);
    if (!err.empty()) {
        out(std::string(method) + " threw: " + err);
        return false;
    }
    if (getter) {
        il2::Obj* r = il2::invoke(il2::find_method(il2::class_of(inst), getter, 0), inst, nullptr, err);
        out(std::string(method) + " -> " + (err.empty() ? (il2::unbox_bool(r) ? "ON" : "OFF") : "? (" + err + ")"));
    } else {
        out(std::string(method) + " done");
    }
    return true;
}

// "Ns.Class" or "Ns.Class#2" -> that live scene object. lists them when there's
// more than one and you didn't pick, since guessing wrong here is confusing
il2::Obj* live_object(const std::string& spec, size_t& idx, const Out& out) {
    std::string cls = spec;
    idx = 0;
    bool picked = false;
    auto hash = spec.rfind('#');
    if (hash != std::string::npos) {
        cls = spec.substr(0, hash);
        idx = (size_t)std::atoi(spec.c_str() + hash + 1);
        picked = true;
    }
    il2::Klass* k = il2::find_class(cls.c_str());
    if (!k) {
        out("class " + cls + " not found");
        return nullptr;
    }
    std::string err;
    auto objs = il2::find_in_scenes(k, err);
    if (objs.empty()) {
        out("no live " + cls + " in the loaded scenes" + (err.empty() ? "" : " (" + err + ")"));
        return nullptr;
    }
    if (idx >= objs.size()) {
        out("only " + std::to_string(objs.size()) + " of those, #0-#" + std::to_string(objs.size() - 1));
        return nullptr;
    }
    if (objs.size() > 1 && !picked) {
        out(std::to_string(objs.size()) + " of those, using #0. pick one with " + cls + "#n:");
        for (size_t i = 0; i < objs.size() && i < 10; ++i) out("  #" + std::to_string(i) + " on '" + il2::object_name(objs[i]) + "'");
    }
    return objs[idx];
}

void print_lines(const Out& out, const std::vector<std::string>& lines, size_t max = 200) {
    for (size_t i = 0; i < lines.size() && i < max; ++i) out(lines[i]);
    if (lines.size() > max) out("... " + std::to_string(lines.size() - max) + " more");
}

// ---------------------------------------------------------- icall helpers

template <typename T>
T icall(const char* name) {
    return reinterpret_cast<T>(api->resolve_icall(name));
}

}  // namespace

void register_all(const LazarusApi* a, LazarusMod* m) {
    api = a;
    self = m;

    // ---- levels

    reg("collections", "list loaded level/scene collections [filter]", [](auto& args, auto& out) {
        auto list = collections(out);
        std::string f = args.size() > 1 ? lower(args[1]) : "";
        int shown = 0;
        for (size_t i = 0; i < list.size(); ++i)
            if (f.empty() || lower(list[i].name).find(f) != std::string::npos) {
                out("#" + std::to_string(i) + "  " + list[i].name);
                ++shown;
            }
        out(std::to_string(shown) + " of " + std::to_string(list.size()) + " collection(s) in memory");
    });

    reg("load", "load a level: load <name part | #index>  (see 'collections')", [](auto& args, auto& out) {
        if (args.size() < 2) {
            out("usage: load <name part | #index>");
            return;
        }
        auto list = collections(out);
        std::vector<Collection*> hits;
        std::string q = args[1];
        if (q[0] == '#') {
            size_t i = (size_t)std::atoi(q.c_str() + 1);
            if (i < list.size()) hits.push_back(&list[i]);
        } else {
            std::string lq = lower(q);
            for (auto& c : list)
                if (lower(c.name) == lq) hits = {&c};
            if (hits.empty())
                for (auto& c : list)
                    if (lower(c.name).find(lq) != std::string::npos) hits.push_back(&c);
        }
        // nothing loaded by that name (main menu, usually) -> the global level list
        std::vector<Collection> global;
        if (hits.empty() && q[0] != '#') {
            global = global_levels(out);
            hits = match(global, q);
        }
        if (hits.empty()) {
            out("no collection matches '" + q + "' (" + std::to_string(list.size()) + " in memory, see 'collections'/'levels')");
            return;
        }
        if (hits.size() > 1) {
            out("'" + q + "' is ambiguous:");
            for (auto* h : hits) out("  " + h->name);
            return;
        }
        std::string err;
        il2::invoke(il2::find_method(il2::class_of(hits[0]->obj), "Load", 0), hits[0]->obj, nullptr, err);
        if (err.empty()) {
            out("loading " + hits[0]->name + " ...");
            api->log(self, LAZARUS_LOG_INFO, "console: loading %s", hits[0]->name.c_str());
        } else {
            out("Load() threw: " + err);
        }
    });

    reg("levels", "the game's full level list, works from the main menu [filter]", [](auto& args, auto& out) {
        auto list = global_levels(out);
        std::string f = args.size() > 1 ? lower(args[1]) : "";
        int shown = 0;
        for (size_t i = 0; i < list.size(); ++i)
            if (f.empty() || lower(list[i].name).find(f) != std::string::npos) {
                out("#" + std::to_string(i) + "  " + list[i].name);
                ++shown;
            }
        out(std::to_string(shown) + " of " + std::to_string(list.size()) + " level(s)");
    });

    reg("start", "start a level as a new game (works from the main menu): start <name part>", [](auto& args, auto& out) {
        if (args.size() < 2) return out("usage: start <name part>   e.g. start TooFarGone");
        auto list = global_levels(out);
        auto hits = match(list, args[1]);
        if (hits.empty()) {
            auto mem = collections(out);
            list.insert(list.end(), mem.begin(), mem.end());
            hits = match(list, args[1]);
        }
        if (hits.empty()) return out("no level matches '" + args[1] + "', see 'levels'");
        if (hits.size() > 1) {
            out("'" + args[1] + "' matches several:");
            for (auto* h : hits) out("  " + h->name);
            return;
        }
        Collection* c = hits[0];
        il2::Klass* ms = il2::find_class("Lucille.Moment.MomentSystem", "Lucille.MomentSystem.dll");
        il2::Method* m = ms ? il2::find_method(ms, "LoadLevel", 2) : nullptr;
        std::string err, cls;
        int t0 = m ? il2::param_type(m, 0, &cls) : -1;
        int t1 = m ? il2::param_type(m, 1) : -1;
        // the game's own path: LoadLevel(collection, isNewGame=true). this is
        // what sets up a playthrough, which a bare Load() from the menu skips.
        // param shapes checked at runtime because the metadata only has names
        if (m && t1 == 0x02 && (t0 == 0x12 || t0 == 0x1c || t0 == 0x0e)) {
            bool is_new = true;
            void* p0 = t0 == 0x0e ? il2::new_string(c->name) : c->obj;
            void* params[] = {p0, &is_new};
            api->log(self, LAZARUS_LOG_INFO, "start: MomentSystem.LoadLevel(%s %s, true)", cls.c_str(), c->name.c_str());
            il2::invoke(m, nullptr, params, err);
            if (err.empty()) return out("starting " + c->name + " as a new game ...");
            out("LoadLevel threw (" + err + "), falling back to a plain load");
        } else {
            out("MomentSystem.LoadLevel isn't the shape we expected (" + std::to_string(t0) + "/" + std::to_string(t1) +
                "), falling back to a plain load");
        }
        err.clear();
        il2::invoke(il2::find_method(il2::class_of(c->obj), "Load", 0), c->obj, nullptr, err);
        out(err.empty() ? "loading " + c->name + " ..." : "Load() threw: " + err);
    });

    reg("items", "item names the game knows [filter] (for 'give')", [](auto& args, auto& out) {
        il2::Obj* im = inventory_manager();
        if (!im) return out("no InventoryManager yet - try from inside a level");
        std::string err;
        il2::Obj* cfg = il2::invoke(il2::find_method(il2::class_of(im), "get_Config", 0), im, nullptr, err);
        il2::Obj* pool = cfg ? il2::invoke(il2::find_method(il2::class_of(cfg), "get_ItemsPool", 0), cfg, nullptr, err) : nullptr;
        if (!pool) return out("couldn't get the item pool" + (err.empty() ? std::string() : ": " + err));
        auto names = il2::struct_strings(pool, "ItemName");
        if (names.empty())
            for (il2::Obj* e : il2::elements(pool))
                if (il2::Obj* s = il2::ref_field(e, "ItemName")) names.push_back(il2::to_utf8(s));
        std::string f = args.size() > 1 ? lower(args[1]) : "";
        std::vector<std::string> shown;
        for (auto& n : names)
            if (f.empty() || lower(n).find(f) != std::string::npos) shown.push_back(n);
        print_lines(out, shown, 300);
        out(std::to_string(shown.size()) + " item(s). cheat shortcuts: rifle shotgun pistol colt sniper crossbow katana machete bat ...");
    });

    reg("give", "give yourself an item: give <name> [amount]  (rifle, shotgun... or see 'items')", [](auto& args, auto& out) {
        if (args.size() < 2) return out("usage: give <name> [amount]");
        int amount = args.size() > 2 ? std::atoi(args[2].c_str()) : 1;
        if (amount < 1) amount = 1;
        // the devs' cheat equips straight into your hands, so prefer it when the
        // cheat object exists. otherwise go through the inventory's test hook
        for (const char* name : kCheatItems) {
            if (lower(name) != lower(args[1])) continue;
            il2::Klass* ck = cheats_class();
            il2::Obj* inst = ck ? il2::static_field(ck, "instance") : nullptr;
            if (inst) {
                std::string method = std::string("Change") + name + "Weapon";
                if (cheat_call(out, method.c_str())) return;
            }
            break;
        }
        il2::Obj* im = inventory_manager();
        if (!im) return out("no InventoryManager yet - try from inside a level");
        il2::Method* m = il2::find_method(il2::class_of(im), "TestAddItem", 2);
        if (!m) return out("InventoryManager.TestAddItem isn't in this build");
        int t0 = il2::param_type(m, 0);
        std::string err;
        int32_t n = amount;
        int64_t id = 0;
        void* p0 = nullptr;
        if (t0 == 0x0e) p0 = il2::new_string(args[1]);
        else if (t0 >= 0x04 && t0 <= 0x0b) {
            char* end = nullptr;
            id = std::strtoll(args[1].c_str(), &end, 10);
            if (*end) return out("TestAddItem wants a numeric id in this build, not a name");
            p0 = &id;
        } else {
            return out("TestAddItem takes a type I don't know how to build (" + std::to_string(t0) + ")");
        }
        void* params[] = {p0, &n};
        api->log(self, LAZARUS_LOG_INFO, "give: TestAddItem(%s, %d)", args[1].c_str(), amount);
        il2::invoke(m, im, params, err);
        out(err.empty() ? "added " + std::to_string(amount) + "x " + args[1] + " (if nothing shows up, check the exact name with 'items')"
                        : "TestAddItem threw: " + err);
    });

    reg("scenes", "list currently loaded Unity scenes", [](auto&, auto& out) {
        auto count = icall<int32_t (*)()>("UnityEngine.SceneManagement.SceneManager::get_sceneCount");
        auto at = icall<void (*)(int32_t, int32_t*)>("UnityEngine.SceneManagement.SceneManager::GetSceneAt_Injected");
        auto name = icall<il2::Obj* (*)(int32_t)>("UnityEngine.SceneManagement.Scene::GetNameInternal");
        if (!count || !at || !name) {
            out("scene icalls unavailable");
            return;
        }
        for (int32_t i = 0, n = count(); i < n; ++i) {
            int32_t h = 0;
            at(i, &h);
            out(std::to_string(i) + ": " + il2::to_utf8(name(h)));
        }
    });

    // ---- the developers' cheat object

    reg("cheats", "show state of the built-in developer cheats", [](auto&, auto& out) {
        il2::Obj* inst = cheats_instance(out);
        if (!inst) return;
        const char* getters[] = {"get_IsGodMode",       "get_IsImmortalMode",    "get_IsInstakill",
                                 "get_IsInfiniteAmmo",  "get_IsInfiniteStamina", "get_IsInfiniteAdrenaline",
                                 "get_IsInvisibleMode", "get_CanRun",            "get_ShowHealthBar"};
        for (auto g : getters) {
            std::string err;
            il2::Obj* r = il2::invoke(il2::find_method(il2::class_of(inst), g, 0), inst, nullptr, err);
            out(std::string(g + 6) + " = " + (err.empty() ? (il2::unbox_bool(r) ? "ON" : "off") : "? " + err));
        }
    });
    reg("cheat", "call any LucilleCheats method: cheat <Method> [int]  (see 'methods Lucille.LucilleCheats')",
        [](auto& args, auto& out) {
            if (args.size() < 2) {
                out("usage: cheat <Method> [int]");
                return;
            }
            if (is_destructive_cheat(args[1])) {
                out(args[1] + " can delete or overwrite your save. if you really mean it: cheat-unsafe " + args[1]);
                return;
            }
            int v = args.size() > 2 ? std::atoi(args[2].c_str()) : 0;
            cheat_call(out, args[1].c_str(), nullptr, args.size() > 2 ? &v : nullptr);
        });
    reg("cheat-unsafe", "like 'cheat' but allows save-deleting/overwriting methods. back up your save first",
        [](auto& args, auto& out) {
            if (args.size() < 2) {
                out("usage: cheat-unsafe <Method> [int]");
                return;
            }
            api->log(self, LAZARUS_LOG_WARN, "cheat-unsafe %s requested", args[1].c_str());
            int v = args.size() > 2 ? std::atoi(args[2].c_str()) : 0;
            cheat_call(out, args[1].c_str(), nullptr, args.size() > 2 ? &v : nullptr);
        });
    reg("god", "toggle god mode", [](auto&, auto& out) { cheat_call(out, "SwitchGodMode", "get_IsGodMode"); });
    reg("immortal", "toggle immortal (take damage, never die)",
        [](auto&, auto& out) { cheat_call(out, "SwitchImmortalMode", "get_IsImmortalMode"); });
    reg("instakill", "toggle one-hit kills", [](auto&, auto& out) { cheat_call(out, "SwitchInstaKill", "get_IsInstakill"); });
    reg("ammo", "toggle infinite ammo", [](auto&, auto& out) { cheat_call(out, "SwitchInfiniteAmmo", "get_IsInfiniteAmmo"); });
    reg("stamina", "toggle infinite stamina",
        [](auto&, auto& out) { cheat_call(out, "SwitchInfiniteStamina", "get_IsInfiniteStamina"); });
    reg("invisible", "toggle enemies ignoring you",
        [](auto&, auto& out) { cheat_call(out, "SwitchInvisibleMode", "get_IsInvisibleMode"); });
    reg("skipobj", "skip the current objective(s): skipobj [count]", [](auto& args, auto& out) {
        int n = args.size() > 1 ? std::atoi(args[1].c_str()) : 1;
        cheat_call(out, "SkipObjectives", nullptr, &n);
    });
    reg("endlevel", "finish the current level", [](auto&, auto& out) { cheat_call(out, "CallEndLevel"); });
    reg("skipnarrative", "skip the current narrative scene",
        [](auto&, auto& out) { cheat_call(out, "CallSkipCurrentNarrative"); });
    reg("skiphq", "skip the current HQ", [](auto&, auto& out) { cheat_call(out, "CallSkipCurrentHQ"); });

    // ---- engine

    reg("timescale", "get/set game speed: timescale [value]", [](auto& args, auto& out) {
        auto get = icall<float (*)()>("UnityEngine.Time::get_timeScale");
        auto set = icall<void (*)(float)>("UnityEngine.Time::set_timeScale");
        if (!get || !set) {
            out("timeScale icalls unavailable");
            return;
        }
        if (args.size() > 1) {
            float v = (float)std::atof(args[1].c_str());
            if (v < 0 || v > 100) {
                out("timescale must be between 0 and 100");
                return;
            }
            set(v);
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "timescale = %.3g", get());
        out(buf);
    });
    reg("fpscap", "get/set frame rate cap: fpscap [n | -1]", [](auto& args, auto& out) {
        auto get = icall<int32_t (*)()>("UnityEngine.Application::get_targetFrameRate");
        auto set = icall<void (*)(int32_t)>("UnityEngine.Application::set_targetFrameRate");
        if (!get || !set) return out("targetFrameRate icalls unavailable");
        if (args.size() > 1) set(std::atoi(args[1].c_str()));
        out("targetFrameRate = " + std::to_string(get()));
    });

    // ---- poking around

    reg("find", "list live objects of a type: find <Ns.Class> [filter]", [](auto& args, auto& out) {
        if (args.size() < 2) return out("usage: find <Ns.Class> [name filter]");
        il2::Klass* k = il2::find_class(args[1].c_str());
        if (!k) return out("class " + args[1] + " not found");
        std::string err;
        auto objs = il2::find_all(k, err);
        if (!err.empty()) return out("failed: " + err);
        std::string f = args.size() > 2 ? lower(args[2]) : "";
        std::vector<std::string> lines;
        for (auto* o : objs) {
            std::string n = il2::object_name(o);
            if (f.empty() || lower(n).find(f) != std::string::npos) lines.push_back(n);
        }
        print_lines(out, lines);
        out(std::to_string(lines.size()) + " object(s)");
    });
    reg("census", "list every component in the loaded scenes [filter]", [](auto& args, auto& out) {
        std::string err;
        auto lines = il2::census(err);
        if (!err.empty()) return out("failed: " + err);
        std::string f = args.size() > 1 ? lower(args[1]) : "";
        std::vector<std::string> hits;
        for (auto& l : lines)
            if (f.empty() || lower(l).find(f) != std::string::npos) hits.push_back(l);
        print_lines(out, hits, 400);
        out(std::to_string(hits.size()) + " distinct component(s)");
    });
    reg("inspect", "dump a live object's fields: inspect <Ns.Class>[#n]", [](auto& args, auto& out) {
        if (args.size() < 2) return out("usage: inspect <Ns.Class>[#n]   e.g. inspect Lucille.BossSystem.BossSystem");
        size_t idx = 0;
        il2::Obj* o = live_object(args[1], idx, out);
        if (!o) return;
        std::vector<std::string> lines;
        if (!il2::describe(o, lines)) return out("this build doesn't export field type info");
        out(il2::class_name(il2::class_of(o)) + " #" + std::to_string(idx) + " on '" + il2::object_name(o) + "':");
        print_lines(out, lines, 300);
    });
    reg("invoke", "call a method on a live object: invoke <Ns.Class>[#n] <Method> [args...]", [](auto& args, auto& out) {
        if (args.size() < 3) return out("usage: invoke <Ns.Class>[#n] <Method> [true|false|number|text|null ...]");
        if (is_destructive_cheat(args[2])) return out(args[2] + " looks like it touches saves, use cheat-unsafe for that");
        size_t idx = 0;
        il2::Obj* o = live_object(args[1], idx, out);
        if (!o) return;
        std::string result, err;
        std::vector<std::string> margs(args.begin() + 3, args.end());
        api->log(self, LAZARUS_LOG_INFO, "invoke %s on %s #%zu", args[2].c_str(), args[1].c_str(), idx);
        if (il2::call_with_args(o, args[2], margs, result, err)) out(args[2] + ": " + result);
        else out(args[2] + " failed: " + err);
    });
    reg("methods", "list methods of a class: methods <Ns.Class> [filter]", [](auto& args, auto& out) {
        if (args.size() < 2) return out("usage: methods <Ns.Class> [filter]");
        il2::Klass* k = il2::find_class(args[1].c_str());
        if (!k) return out("class " + args[1] + " not found");
        std::string f = args.size() > 2 ? lower(args[2]) : "";
        std::vector<std::string> lines;
        for (auto& md : il2::methods_of(k))
            if (f.empty() || lower(md.name).find(f) != std::string::npos)
                lines.push_back((md.is_static ? "static " : "") + md.name + " (" + std::to_string(md.argc) + " args)");
        print_lines(out, lines);
    });
    reg("fields", "list fields of a class: fields <Ns.Class>", [](auto& args, auto& out) {
        if (args.size() < 2) return out("usage: fields <Ns.Class>");
        il2::Klass* k = il2::find_class(args[1].c_str());
        if (!k) return out("class " + args[1] + " not found");
        print_lines(out, il2::fields_of(k));
    });
    reg("call", "call a parameterless static method: call <Ns.Class> <Method>", [](auto& args, auto& out) {
        if (args.size() < 3) return out("usage: call <Ns.Class> <Method>");
        il2::Klass* k = il2::find_class(args[1].c_str());
        if (!k) return out("class " + args[1] + " not found");
        std::string err;
        il2::Obj* r = il2::invoke(il2::find_method(k, args[2].c_str(), 0), nullptr, nullptr, err);
        if (!err.empty()) return out("threw: " + err);
        out(r ? "returned " + il2::class_name(il2::class_of(r)) : "returned (void/null)");
    });
}

}  // namespace con
