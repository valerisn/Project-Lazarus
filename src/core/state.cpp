#include "core/state.h"

#include <windows.h>
#include <bcrypt.h>

#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

#include "core/ini.h"

#pragma comment(lib, "bcrypt.lib")

namespace lz {
namespace fs = std::filesystem;

static const KnownBuild kKnownBuilds[] = {
    // steam build 12839205 (unity 2021.3.17f1). only PC build that ever shipped afaik
    {"1b579b75b1486c0e", "Steam build 12839205 (December 2023 release)"},
};

const KnownBuild* find_known_build(const std::string& fp) {
    for (auto& b : kKnownBuilds)
        if (fp == b.fingerprint) return &b;
    return nullptr;
}

BootGuard::BootGuard(fs::path state_dir) : dir_(std::move(state_dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
}

std::string BootGuard::take_stale_lock() {
    std::error_code ec;
    if (!fs::exists(lock_path(), ec)) return "";
    std::ifstream f(lock_path(), std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    fs::remove(lock_path(), ec);
    s = trim(s);
    return s.empty() ? "<unknown>" : s;
}

void BootGuard::begin(const std::string& what) {
    // no FlushFileBuffers. a crashed process doesn't lose the OS cache, a
    // power cut would, and then nobody cares about mod quarantine
    std::ofstream f(lock_path(), std::ios::binary | std::ios::trunc);
    f << what;
}

void BootGuard::end() {
    std::error_code ec;
    fs::remove(lock_path(), ec);
}

std::set<std::string> BootGuard::load_quarantine() const {
    std::set<std::string> out;
    std::ifstream f(quarantine_path());
    std::string line;
    while (std::getline(f, line)) {
        auto t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        auto bar = t.find('|');
        out.insert(to_lower(trim(t.substr(0, bar))));
    }
    return out;
}

void BootGuard::quarantine(const std::string& id, const std::string& reason) const {
    bool fresh = !fs::exists(quarantine_path());
    std::ofstream f(quarantine_path(), std::ios::app);
    if (fresh)
        f << "; Mods listed here crashed the game during startup and will not be loaded.\n"
             "; Delete a line (or this whole file) to try that mod again.\n";
    std::time_t t = std::time(nullptr);
    char ts[32];
    std::tm tm{};
    localtime_s(&tm, &t);
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M", &tm);
    f << id << " | " << ts << " | " << reason << "\n";
}

std::string sha256_hex(const fs::path& file) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    BCRYPT_HASH_HANDLE hh = nullptr;
    std::string result;
    // pseudo-handle, saves the open/close provider dance. win10+ only, fine
    if (BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hh, nullptr, 0, nullptr, 0, 0))) {
        std::vector<unsigned char> buf(1 << 20);
        DWORD got = 0;
        bool ok = true;
        while (ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr) && got)
            if (!BCRYPT_SUCCESS(BCryptHashData(hh, buf.data(), got, 0))) {
                ok = false;
                break;
            }
        unsigned char digest[32];
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hh, digest, sizeof digest, 0))) {
            static const char* hex = "0123456789abcdef";
            for (auto b : digest) {
                result += hex[b >> 4];
                result += hex[b & 15];
            }
        }
        BCryptDestroyHash(hh);
    }
    CloseHandle(h);
    return result;
}

std::string build_fingerprint(const fs::path& ga, const fs::path& cache_file) {
    std::error_code ec;
    auto size = fs::file_size(ga, ec);
    if (ec) return "";
    auto mtime = fs::last_write_time(ga, ec).time_since_epoch().count();
    std::ostringstream key;
    key << size << "|" << mtime;

    {
        std::ifstream f(cache_file);
        std::string line;
        if (std::getline(f, line)) {
            auto bar = line.rfind('|');
            if (bar != std::string::npos && line.substr(0, bar) == key.str()) {
                auto fp = trim(line.substr(bar + 1));
                if (fp.size() == 16) return fp;
            }
        }
    }

    auto full = sha256_hex(ga);
    if (full.empty()) return "";
    auto fp = full.substr(0, 16);
    fs::create_directories(cache_file.parent_path(), ec);
    std::ofstream(cache_file, std::ios::trunc) << key.str() << "|" << fp << "\n";
    return fp;
}

}  // namespace lz
