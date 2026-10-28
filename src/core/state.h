#pragma once
// crash recovery + build fingerprint, both under Lazarus/state/.
//
// crash recovery is deliberately stupid: write the mod id to boot.lock before
// its init, delete it after. game dies mid-init -> file's still there next
// launch -> that mod gets quarantined. works even when the process gets
// killed outright, which no crash handler would catch.

#include <filesystem>
#include <set>
#include <string>

namespace lz {

class BootGuard {
public:
    explicit BootGuard(std::filesystem::path state_dir);

    // "" if last session was fine. deletes the lock as it reads it
    std::string take_stale_lock();

    void begin(const std::string& what);
    void end();

    std::set<std::string> load_quarantine() const;
    void quarantine(const std::string& id, const std::string& reason) const;

    std::filesystem::path lock_path() const { return dir_ / "boot.lock"; }
    std::filesystem::path quarantine_path() const { return dir_ / "quarantine.txt"; }

private:
    std::filesystem::path dir_;
};

// first 16 hex chars of sha256(GameAssembly.dll). every code patch changes
// that file, so it's a better "which build" signal than anything from Steam.
// cached on size+mtime, hashing 59MB every launch is ~100ms wasted
std::string build_fingerprint(const std::filesystem::path& game_assembly, const std::filesystem::path& cache_file);
std::string sha256_hex(const std::filesystem::path& file);

struct KnownBuild {
    const char* fingerprint;
    const char* description;
};
const KnownBuild* find_known_build(const std::string& fingerprint);

}  // namespace lz
