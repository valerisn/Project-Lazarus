#include "../mods/method-trace/trace_config.h"
#include "testing.h"

using namespace trace;

TEST_CASE("trace: target lines") {
    Target t;
    std::string err;
    CHECK(parse_target(" Lucille.BossSystem.dll | Lucille.BossSystem.BossSystem |StartBossPart| 0 ", t, err));
    CHECK_EQ(t.assembly, "Lucille.BossSystem.dll");
    CHECK_EQ(t.klass, "Lucille.BossSystem.BossSystem");
    CHECK_EQ(t.method, "StartBossPart");
    CHECK_EQ(t.argc, 0);
    CHECK(parse_target("a.dll|A.B|M|3", t, err) && t.argc == 3);
    CHECK(!parse_target("a.dll|A.B|M", t, err));
    CHECK(!parse_target("a.dll|A.B|M|4", t, err));    // can't fit in registers anyway
    CHECK(!parse_target("a.dll|A.B|M|x", t, err));
    CHECK(!parse_target("a.dll||M|0", t, err));
}

TEST_CASE("trace: type classification") {
    CHECK(classify(0x01, false, false) == Kind::Void);
    CHECK(classify(0x02, true, false) == Kind::Bool);
    CHECK(classify(0x08, true, false) == Kind::Int32);
    CHECK(classify(0x0c, true, false) == Kind::Unsupported);  // float
    CHECK(classify(0x0d, true, false) == Kind::Unsupported);  // double
    CHECK(classify(0x11, true, true) == Kind::Enum);
    CHECK(classify(0x11, true, false) == Kind::Unsupported);  // Vector3 and friends
    CHECK(classify(0x15, false, false) == Kind::Object);
    CHECK(classify(0x15, true, false) == Kind::Unsupported);
    CHECK(classify(0x0e, false, false) == Kind::String);
    CHECK(classify(0x12, false, false) == Kind::Object);
}

TEST_CASE("trace: register budget") {
    CHECK(fits_registers(false, 2));   // this, a, b, MethodInfo*
    CHECK(!fits_registers(false, 3));
    CHECK(fits_registers(true, 3));
    CHECK(!fits_registers(true, 4));
}
