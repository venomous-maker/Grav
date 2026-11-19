#include "driver/build_cache.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace grav {

namespace {

// What `cmd` prints ("" when it cannot be run).
std::string output(const std::string &cmd) {
    std::string out;
    if (FILE *p = popen((cmd + " 2>/dev/null").c_str(), "r")) {
        char buf[512];
        while (fgets(buf, sizeof buf, p)) out += buf;
        pclose(p);
    }
    return out;
}

// Two FNV-1a hashes with different starting points: 128 bits, as hex. (A cache
// key, not a defence against anyone crafting collisions.)
struct Hasher {
    unsigned long long a = 1469598103934665603ULL, b = 0x6c62272e07bb0142ULL;
    void add(const char *p, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            a = (a ^ static_cast<unsigned char>(p[i])) * 1099511628211ULL;
            b = (b ^ static_cast<unsigned char>(p[i])) * 0x100000001b3ULL;
            b ^= b >> 29;
        }
    }
    void add(const std::string &s) { add(s.data(), s.size()); add("\0", 1); }
    std::string hex() const {
        char buf[33];
        std::snprintf(buf, sizeof buf, "%016llx%016llx", a, b);
        return buf;
    }
};

fs::path cacheDir() {
    if (const char *d = std::getenv("GRAV_CACHE"); d && *d) return d;
    if (const char *x = std::getenv("XDG_CACHE_HOME"); x && *x) return fs::path(x) / "grav" / "builds";
    if (const char *h = std::getenv("HOME"); h && *h) return fs::path(h) / ".cache" / "grav" / "builds";
    return "";
}

} // namespace

bool ccIsGcc(const std::string &cc) {
    // (Debian's says "cc (Debian 16.2.0-3) 16.2.0" first; the rest names the FSF.
    // clang's mentions neither.)
    std::string v = output(cc + " --version");
    return v.find("Free Software Foundation") != std::string::npos || v.find("gcc") != std::string::npos;
}

BuildCache::BuildCache(const std::string &cPath, const std::string &cc, const std::string &flags) {
    if (const char *off = std::getenv("GRAV_BUILD_CACHE"); off && std::string(off) == "0") return;
    fs::path dir = cacheDir();
    std::ifstream in(cPath, std::ios::binary);
    if (dir.empty() || !in) return;
    std::string c((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Hasher h;
    h.add(cc);
    h.add(output(cc + " --version")); // a new compiler builds afresh
    h.add(flags);
    // The C names itself in its #line directives, and a build's C is usually a
    // temporary file named after the process: its name is left out.
    size_t at = 0;
    for (size_t hit; (hit = c.find(cPath, at)) != std::string::npos; at = hit + cPath.size())
        h.add(c.data() + at, hit - at);
    h.add(c.data() + at, c.size() - at);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;
    entry_ = (dir / h.hex()).string();
}

BuildCache::BuildCache(const std::vector<std::string> &parts) {
    if (const char *off = std::getenv("GRAV_BUILD_CACHE"); off && std::string(off) == "0") return;
    fs::path dir = cacheDir();
    if (dir.empty()) return;
    Hasher h;
    for (const auto &p : parts) h.add(p);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;
    entry_ = (dir / ("p" + h.hex())).string();
}

bool BuildCache::restore(const std::string &exePath) const {
    if (entry_.empty()) return false;
    std::error_code ec;
    if (!fs::is_regular_file(entry_, ec)) return false;
    fs::copy_file(entry_, exePath, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::last_write_time(entry_, fs::file_time_type::clock::now(), ec); // (recently used)
    return true;
}

void BuildCache::store(const std::string &exePath) const {
    if (entry_.empty()) return;
    std::error_code ec;
    // Copy then rename, so a build running at the same time never sees half a file.
    std::string tmp = entry_ + ".tmp" + std::to_string(::getpid());
    fs::copy_file(exePath, tmp, fs::copy_options::overwrite_existing, ec);
    if (!ec) fs::rename(tmp, entry_, ec);
    if (ec) { fs::remove(tmp, ec); return; }
    // Keep the most recently used entries.
    size_t keep = 40;
    if (const char *m = std::getenv("GRAV_CACHE_MAX"); m && std::atoi(m) > 0) keep = static_cast<size_t>(std::atoi(m));
    std::vector<std::pair<fs::file_time_type, fs::path>> all;
    for (const auto &e : fs::directory_iterator(fs::path(entry_).parent_path(), ec))
        if (e.is_regular_file(ec) && e.path().filename().string().find(".tmp") == std::string::npos)
            all.push_back({e.last_write_time(ec), e.path()});
    if (all.size() <= keep) return;
    std::sort(all.begin(), all.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
    for (size_t i = keep; i < all.size(); ++i) fs::remove(all[i].second, ec);
}

} // namespace grav
