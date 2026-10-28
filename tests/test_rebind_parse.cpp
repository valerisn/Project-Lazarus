#include "../mods/key-rebind/rebind_parse.h"
#include "testing.h"

using namespace rebind;

TEST_CASE("rebind: valid line") {
    Rebind r;
    std::string err;
    CHECK(parse(" Player/Interact ", " <Keyboard>/e  ->  <Keyboard>/f ", r, err));
    CHECK_EQ(r.action, "Player/Interact");
    CHECK_EQ(r.from, "<Keyboard>/e");
    CHECK_EQ(r.to, "<Keyboard>/f");
    CHECK(parse("fire", "<Mouse>/leftButton-><Keyboard>/space", r, err));
    CHECK_EQ(r.to, "<Keyboard>/space");
}

TEST_CASE("rebind: bad lines explain themselves") {
    Rebind r;
    std::string err;
    CHECK(!parse("jump", "<Keyboard>/space", r, err));
    CHECK(err.find("->") != std::string::npos);
    CHECK(!parse("jump", "space -> <Keyboard>/j", r, err));
    CHECK(err.find("space") != std::string::npos);
    CHECK(!parse("jump", "<Keyboard>/space -> j", r, err));
    CHECK(!parse("jump", "<Keyboard> -> <Keyboard>/j", r, err));
    CHECK(!parse("  ", "<Keyboard>/a -> <Keyboard>/b", r, err));
}
