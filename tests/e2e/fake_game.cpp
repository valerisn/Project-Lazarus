// fake game exe. the real one is also just a stub that imports UnityPlayer.dll

#include <windows.h>

extern "C" __declspec(dllimport) int fake_unity_main(const wchar_t* report_path);

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 99;
    return fake_unity_main(argv[1]);
}
