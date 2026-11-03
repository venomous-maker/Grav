// Module loading for the driver: resolves `import "..."` lines, finds the
// standard library and the project's grav.toml, and splices modules together.
#ifndef GRAV_DRIVER_MODULES_H
#define GRAV_DRIVER_MODULES_H

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace grav {

// Where a line of the combined source originally came from.
struct SrcLoc {
    std::string file;
    int line = 0;
};

// grav.toml, found by searching upward from a directory:
//   main = "src/main.grav"        # what `grav run` / `grav test` build by default
//   paths = ["lib", "vendor"]     # extra import roots (relative to grav.toml)
struct ProjectConfig {
    bool found = false;
    std::filesystem::path root; // directory holding grav.toml
    std::string name;
    std::filesystem::path main;
    std::vector<std::filesystem::path> paths;
};

ProjectConfig findProject(const std::filesystem::path &start, std::string &err);

// The standard library directory ($GRAV_STD, $GRAV_HOME/lib, next to the
// executable, or the source tree it was built from); empty if none is found.
std::filesystem::path stdLibDir();

class ModuleLoader {
public:
    explicit ModuleLoader(ProjectConfig project) : project_(std::move(project)) {}

    // Load `path` and, recursively, its imports (each module at most once).
    // Fills `srcMap` so combined line N (1-based) maps to its original file:line.
    bool load(const std::filesystem::path &path, std::string &out, std::vector<SrcLoc> &srcMap,
              std::string &err);

private:
    // Resolve an import spec written in `fromDir`; empty path when not found.
    std::filesystem::path resolve(const std::string &spec, const std::filesystem::path &fromDir,
                                  std::vector<std::string> &tried) const;

    ProjectConfig project_;
    std::set<std::string> visited_;
};

} // namespace grav

#endif // GRAV_DRIVER_MODULES_H
