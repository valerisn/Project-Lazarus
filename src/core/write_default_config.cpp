// build step: dumps kDefaultConfigText to argv[1] so the shipped lazarus.ini can't drift from the code
#include <cstdio>

#include "core/config.h"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    FILE* f = std::fopen(argv[1], "wb");
    if (!f) return 1;
    std::fputs(lz::kDefaultConfigText, f);
    return std::fclose(f) == 0 ? 0 : 1;
}
