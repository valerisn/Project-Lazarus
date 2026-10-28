#pragma once
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <windows.h>

// temp dir that deletes itself. unique name per test in case these ever run in parallel
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        static std::atomic<int> n{0};
        path = std::filesystem::temp_directory_path() /
               ("lazarus_test_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(n++));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

inline void write_file(const std::filesystem::path& p, const std::string& s) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
}

inline std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
