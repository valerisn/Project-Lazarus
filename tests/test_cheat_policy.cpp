#include "../mods/dev-console/cheat_policy.h"
#include "testing.h"

using con::is_destructive_cheat;

TEST_CASE("cheat policy: save-touching methods need cheat-unsafe") {
    // the real ones from Lucille.LucilleCheats
    CHECK(is_destructive_cheat("CallOnDeleteMainSave"));
    CHECK(is_destructive_cheat("CallOnCreateMainSave"));
    CHECK(is_destructive_cheat("CreateNewSave"));
    CHECK(is_destructive_cheat("callondeletemainsave"));  // case doesn't help you sneak past
    CHECK(!is_destructive_cheat("SwitchGodMode"));
    CHECK(!is_destructive_cheat("AddSkillPoints"));
    CHECK(!is_destructive_cheat("CallEndLevel"));
}
