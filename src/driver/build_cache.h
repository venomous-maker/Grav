#ifndef GRAV_DRIVER_BUILD_CACHE_H
#define GRAV_DRIVER_BUILD_CACHE_H

#include <string>
#include <vector>

namespace grav {

// Built binaries, kept by what made them: the generated C, the C compiler (its
// version line) and the command's flags. Building the same program the same way
// again copies the binary out instead of running the C compiler, which for a
// large program is most of the build. The cache lives in $GRAV_CACHE, else
// $XDG_CACHE_HOME/grav/builds, else ~/.cache/grav/builds, and keeps the most
// recently used entries (GRAV_CACHE_MAX, default 40). GRAV_BUILD_CACHE=0 turns it off.
class BuildCache {
public:
    // `flags` is everything about the C compiler command except the file names.
    BuildCache(const std::string &cPath, const std::string &cc, const std::string &flags);
    // A whole build: `parts` are everything it depends on (the program's source, its
    // files, the compiler and every option), so a hit skips even the front end.
    explicit BuildCache(const std::vector<std::string> &parts);
    bool enabled() const { return !entry_.empty(); }
    // Copies a cached binary to `exePath`; whether there was one.
    bool restore(const std::string &exePath) const;
    // Keeps the binary just built at `exePath` (and trims the cache).
    void store(const std::string &exePath) const;

private:
    std::string entry_; // the cached binary's path, "" when caching is off
};

// Whether `cc` is GCC, which optimizes one large file in parallel with -flto=auto.
bool ccIsGcc(const std::string &cc);

} // namespace grav

#endif // GRAV_DRIVER_BUILD_CACHE_H
