#pragma once
#include "lazarus/lazarus_api.h"

namespace con {
// registers every console command with the loader. main thread.
void register_all(const LazarusApi* api, LazarusMod* self);
}
