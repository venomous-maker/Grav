// Module loading for the driver: resolves `import "..."` lines, finds the
// standard library and the project's grav.toml, and splices modules together.
#ifndef GRAV_DRIVER_MODULES_H
#define GRAV_DRIVER_MODULES_H

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace grav {

// Where a line of the combined source originally came from.
struct SrcLoc {
    std::string file;
    int line = 0;
};

// A third-party package listed under [dependencies] in grav.toml:
//   json5 = "github.com/someone/json5@v1.2.0"
//   http2 = { git = "https://gitlab.com/team/http2", version = "v0.3.1" }
struct Dependency {
    std::string name;    // what imports use: import "json5" / "json5/parser"
    std::string source;  // "github.com/someone/json5", a URL, or a local path
    std::string version; // tag, branch or commit ("" = newest tag, else the default branch)
};

// grav.lock pins each dependency to the commit that was installed.
struct LockEntry {
    std::string source, version, commit;
};

// grav.toml, found by searching upward from a directory:
//   name = "app"
//   main = "src/main.grav"        # what `grav run` / `grav test` build by default
//   lib = "src/lib.grav"          # a package's entry point for `import "<name>"`
//   paths = ["lib", "vendor"]     # extra import roots (relative to grav.toml)
//   [dependencies]
//   json5 = "github.com/someone/json5@v1.2.0"
struct ProjectConfig {
    bool found = false;
    std::filesystem::path root; // directory holding grav.toml
    std::string name;
    std::filesystem::path main;
    std::filesystem::path lib;
    std::vector<std::filesystem::path> paths;
    std::vector<Dependency> deps;
    std::vector<std::pair<std::string, LockEntry>> lock; // from grav.lock
    const LockEntry *locked(const std::string &name) const {
        for (const auto &[n, e] : lock) if (n == name) return &e;
        return nullptr;
    }
};

// Where packages are cloned: $GRAV_PACKAGES, else ~/.local/grav/packages.
std::filesystem::path packagesDir();
// "github.com/a/b" -> "github.com/a/b"; URLs and paths -> a cache-friendly key.
std::string sourceKey(const std::string &source);
// The installed directory of a dependency at a version or commit.
std::filesystem::path packagePath(const std::string &source, const std::string &versionOrCommit);
// Where `dep` is installed (from grav.lock, else any installed version); empty if not installed.
std::filesystem::path installedPackage(const ProjectConfig &project, const Dependency &dep);
// Parse one dependency value (`"src@ver"` or `{ git = ..., version = ... }`).
Dependency parseDependency(const std::string &name, const std::string &value);

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
    // Namespaces in which a loaded module declares `name` (for "did you mean" hints).
    std::vector<std::string> whereIs(const std::string &name) const;

private:
    // Resolve an import spec written in `fromDir`; empty path when not found.
    std::filesystem::path resolve(const std::string &spec, const std::filesystem::path &fromDir,
                                  std::vector<std::string> &tried);
    // The project (or package) a directory belongs to, cached.
    const ProjectConfig &projectOf(const std::filesystem::path &dir);

    // What the loader learned about each file: the namespaces it opens, the
    // names it declares (name -> namespace), and the files it imports.
    struct ModInfo {
        std::vector<std::string> namespaces;
        std::vector<std::pair<std::string, std::string>> names;
        std::vector<std::string> imports;
    };
    bool isLibrary(const std::filesystem::path &p) const; // under the std dir
    std::vector<std::string> closure(const std::string &key) const;
    // The `use` line an import adds to the importing scope (see load()).
    bool importUses(const std::string &key, const std::string &spec, const std::string &alias,
                    const std::vector<std::pair<std::string, std::string>> &only, std::string &uses,
                    std::string &err) const;

    ProjectConfig project_;
    std::set<std::string> visited_;
    std::vector<std::pair<std::string, ProjectConfig>> projects_;
    std::map<std::string, ModInfo> info_;
    bool preludeLoaded_ = false;
};

} // namespace grav

#endif // GRAV_DRIVER_MODULES_H
