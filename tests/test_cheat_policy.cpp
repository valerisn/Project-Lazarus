#include "../mods/dev-console/cheat_policy.h"
#include "testing.h"

using con::is_destructive_cheat;

TEST_CASE("cheat policy: save-touching methods need cheat-unsafe") {
    // real method names from LucilleCheats
    CHECK(is_destructive_cheat("CallOnDeleteMainSave"));
    CHECK(is_destructive_cheat("CallOnCreateMainSave"));
    CHECK(is_destructive_cheat("CreateNewSave"));
    CHECK(is_destructive_cheat("callondeletemainsave"));  // lowercase doesn't get you past it
    CHECK(!is_destructive_cheat("SwitchGodMode"));
    CHECK(!is_destructive_cheat("AddSkillPoints"));
    CHECK(!is_destructive_cheat("CallEndLevel"));
}
