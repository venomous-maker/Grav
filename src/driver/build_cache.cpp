#include "driver/build_cache.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

std::string buildCacheDir(const std::string &sub) {
    if (const char *off = std::getenv("GRAV_BUILD_CACHE"); off && std::string(off) == "0") return "";
    fs::path dir = cacheDir();
    if (dir.empty()) return "";
    std::error_code ec;
    fs::create_directories(dir / sub, ec);
    return ec ? "" : (dir / sub).string();
}

bool ccIsGcc(const std::string &cc) {
    // (Debian's says "cc (Debian 16.2.0-3) 16.2.0" first; the rest names the FSF.
    // clang's mentions neither.)
    std::string v = output(cc + " --version");
    return v.find("Free Software Foundation") != std::string::npos || v.find("gcc") != std::string::npos;
}

std::string ltoIncrementalDir(const std::string &cc) {
    if (const char *off = std::getenv("GRAV_BUILD_CACHE"); off && std::string(off) == "0") return "";
    fs::path dir = cacheDir();
    if (dir.empty() || output(cc + " --help=common").find("-flto-incremental=") == std::string::npos) return "";
    std::error_code ec;
    fs::create_directories(dir / "lto", ec);
    return ec ? "" : (dir / "lto").string();
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

int ltoCacheEntries() {
    if (const char *n = std::getenv("GRAV_CACHE_LTO_ENTRIES"); n && std::atoi(n) > 0) return std::atoi(n);
    return 1024;
}

namespace {

struct Usage {
    uintmax_t bytes = 0;
    size_t files = 0;
};

Usage usageOf(const fs::path &p) {
    Usage u;
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) return {fs::file_size(p, ec), 1};
    for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        u.bytes += it->file_size(ec);
        ++u.files;
    }
    return u;
}

std::string human(uintmax_t b) {
    char buf[32];
    if (b >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.1f GB", b / double(1ull << 30));
    else if (b >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.1f MB", b / double(1ull << 20));
    else if (b >= 1024) std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
    else std::snprintf(buf, sizeof buf, "%ju B", b);
    return buf;
}

// The built programs: the files at the top of the cache folder.
std::vector<fs::path> binaries(const fs::path &dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) out.push_back(e.path());
    return out;
}

} // namespace

int runCacheCommand(const std::vector<std::string> &args) {
    fs::path dir = cacheDir();
    if (dir.empty()) { std::cerr << "grav cache: no cache folder (set GRAV_CACHE or HOME)\n"; return 1; }
    std::string what = args.empty() ? "info" : args[0];
    std::error_code ec;
    if (what == "info") {
        if (args.size() > 1) { std::cerr << "usage: grav cache [info] | grav cache clean [binaries|lto|infer]\n"; return 2; }
        Usage bins;
        for (const auto &b : binaries(dir)) { bins.bytes += usageOf(b).bytes; ++bins.files; }
        Usage lto = usageOf(dir / "lto"), infer = usageOf(dir / "infer");
        std::cout << "build cache: " << dir.string() << "\n"
                  << "  binaries  " << human(bins.bytes) << " (" << bins.files << " built programs, keeps "
                  << (std::getenv("GRAV_CACHE_MAX") ? std::getenv("GRAV_CACHE_MAX") : "40") << ")\n"
                  << "  lto       " << human(lto.bytes) << " (GCC's link-time partitions, keeps " << ltoCacheEntries() << ")\n"
                  << "  infer     " << human(infer.bytes) << " (inferred type arguments)\n"
                  << "  total     " << human(bins.bytes + lto.bytes + infer.bytes) << "\n"
                  << "grav cache clean removes it all (the next build of each program takes longer)\n";
        return 0;
    }
    if (what == "clean") {
        std::string part = args.size() > 1 ? args[1] : "all";
        if (args.size() > 2 || (part != "all" && part != "binaries" && part != "lto" && part != "infer")) {
            std::cerr << "usage: grav cache clean [binaries|lto|infer]\n";
            return 2;
        }
        Usage freed;
        auto drop = [&](const fs::path &p) {
            Usage u = usageOf(p);
            fs::remove_all(p, ec);
            if (ec) { std::cerr << "grav cache: cannot remove " << p.string() << ": " << ec.message() << "\n"; return; }
            freed.bytes += u.bytes;
            freed.files += u.files;
        };
        if (part == "all" || part == "binaries") for (const auto &b : binaries(dir)) drop(b);
        if (part == "all" || part == "lto") drop(dir / "lto");
        if (part == "all" || part == "infer") drop(dir / "infer");
        std::cout << "grav cache: removed " << freed.files << " files, " << human(freed.bytes) << "\n";
        return ec ? 1 : 0;
    }
    std::cerr << "usage: grav cache [info] | grav cache clean [binaries|lto|infer]\n";
    return 2;
}

} // namespace grav
