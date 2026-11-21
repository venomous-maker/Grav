#include "driver/modules.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <functional>
#include <sstream>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace grav {

namespace {

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

// The quoted strings in a TOML value: "x" or ["a", "b"].
std::vector<std::string> quotedStrings(const std::string &v) {
    std::vector<std::string> out;
    size_t i = 0;
    while ((i = v.find('"', i)) != std::string::npos) {
        size_t j = v.find('"', i + 1);
        if (j == std::string::npos) break;
        out.push_back(v.substr(i + 1, j - i - 1));
        i = j + 1;
    }
    return out;
}

bool isFile(const fs::path &p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool hasStd(const fs::path &dir) { return !dir.empty() && isFile(dir / "std.grav"); }

} // namespace

fs::path packagesDir() {
    if (const char *e = std::getenv("GRAV_PACKAGES"); e && *e) return e;
    const char *home = std::getenv("HOME");
    return fs::path(home && *home ? home : "/tmp") / ".local" / "grav" / "packages";
}

std::string sourceKey(const std::string &source) {
    std::string s = source;
    for (const char *p : {"https://", "http://", "ssh://", "git://", "file://"})
        if (s.rfind(p, 0) == 0) { s = s.substr(std::string(p).size()); break; }
    if (s.rfind("git@", 0) == 0) { s = s.substr(4); std::replace(s.begin(), s.end(), ':', '/'); }
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".git") == 0) s = s.substr(0, s.size() - 4);
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (!s.empty() && s[0] == '/') s = "local" + s;            // a local repository
    std::string out;
    for (char c : s) out += (c == '.' && out.empty()) ? '_' : c;
    return out;
}

fs::path packagePath(const std::string &source, const std::string &versionOrCommit) {
    fs::path key = sourceKey(source);
    return packagesDir() / key.parent_path() / (key.filename().string() + "@" + versionOrCommit);
}

fs::path installedPackage(const ProjectConfig &project, const Dependency &dep) {
    std::error_code ec;
    if (dep.local) {                                           // read in place
        fs::path p = dep.source;
        if (!p.empty() && p.string()[0] == '~')
            if (const char *home = std::getenv("HOME")) p = fs::path(home) / p.string().substr(p.string().size() > 1 ? 2 : 1);
        if (p.is_relative()) p = project.root / p;
        p = fs::weakly_canonical(p, ec);
        return fs::is_directory(p, ec) ? p : fs::path();
    }
    if (const LockEntry *l = project.locked(dep.name); l && l->source == dep.source) {
        fs::path p = packagePath(dep.source, l->version.empty() ? l->commit.substr(0, 12) : l->version);
        if (fs::is_directory(p, ec)) return p;
    }
    if (!dep.version.empty()) {
        fs::path p = packagePath(dep.source, dep.version);
        if (fs::is_directory(p, ec)) return p;
    }
    // Any installed version (newest first by modification time).
    fs::path key = sourceKey(dep.source);
    fs::path dir = packagesDir() / key.parent_path();
    std::string prefix = key.filename().string() + "@";
    fs::path best;
    fs::file_time_type bestTime{};
    if (fs::is_directory(dir, ec))
        for (const auto &e : fs::directory_iterator(dir, ec)) {
            std::string n = e.path().filename().string();
            if (n.rfind(prefix, 0) != 0 || !e.is_directory()) continue;
            auto t = e.last_write_time(ec);
            if (best.empty() || t > bestTime) { best = e.path(); bestTime = t; }
        }
    return best;
}

Dependency parseDependency(const std::string &name, const std::string &value) {
    Dependency d;
    d.name = name;
    std::string v = trim(value);
    if (!v.empty() && v[0] == '{') {               // { git = "...", version = "..." }
        std::string body = v.substr(1, v.rfind('}') == std::string::npos ? std::string::npos : v.rfind('}') - 1);
        size_t i = 0;
        while (i < body.size()) {
            size_t eq = body.find('=', i);
            if (eq == std::string::npos) break;
            std::string key = trim(body.substr(i, eq - i));
            if (!key.empty() && key[0] == ',') key = trim(key.substr(1));
            auto q = quotedStrings(body.substr(eq + 1));
            size_t q1 = body.find('"', eq), q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
            std::string val = q.empty() ? "" : q[0];
            if (key == "git" || key == "source") d.source = val;
            else if (key == "path") { d.source = val; d.local = true; }
            else if (key == "version" || key == "tag" || key == "branch" || key == "rev") d.version = val;
            i = q2 == std::string::npos ? body.size() : q2 + 1;
        }
        return d;
    }
    auto q = quotedStrings(v);
    std::string spec = q.empty() ? v : q[0];
    // "source@version" — but not the "git@host:" of an SSH URL.
    size_t at = spec.rfind('@');
    if (at != std::string::npos && at > 0 && spec.find('/', at) == std::string::npos &&
        !(spec.rfind("git@", 0) == 0 && at == 3)) {
        d.source = spec.substr(0, at);
        d.version = spec.substr(at + 1);
    } else {
        d.source = spec;
    }
    return d;
}

static void readLock(ProjectConfig &cfg) {
    std::ifstream in(cfg.root / "grav.lock");
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == '[') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string name = trim(t.substr(0, eq));
        std::string body = t.substr(eq + 1);
        LockEntry e;
        auto field = [&](const std::string &k) -> std::string {
            size_t p = body.find(k + " = \"");
            if (p == std::string::npos) return "";
            size_t s = p + k.size() + 4, end = body.find('"', s);
            return end == std::string::npos ? "" : body.substr(s, end - s);
        };
        e.source = field("source");
        e.version = field("version");
        e.commit = field("commit");
        cfg.lock.push_back({name, e});
    }
}

// "1.7.2" (or "1.7", "v1.7.2-rc1") -> {1, 7, 2}.
static std::array<long, 3> versionParts(const std::string &v) {
    std::array<long, 3> out{0, 0, 0};
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    for (int part = 0; part < 3 && i < v.size(); part++) {
        long n = 0;
        while (i < v.size() && std::isdigit((unsigned char)v[i])) n = n * 10 + (v[i++] - '0');
        out[part] = n;
        if (i < v.size() && v[i] == '.') i++;
        else break;
    }
    return out;
}

bool versionSatisfies(const std::string &version, const std::string &requirement) {
    auto have = versionParts(version);
    std::stringstream ss(requirement);
    std::string clause;
    while (std::getline(ss, clause, ',')) {
        std::string c = trim(clause);
        if (c.empty()) continue;
        size_t n = 0;
        while (n < c.size() && std::string("<>=^~").find(c[n]) != std::string::npos) n++;
        std::string op = c.substr(0, n);
        auto want = versionParts(trim(c.substr(n)));
        bool ok;
        if (op.empty() || op == ">=") ok = have >= want;
        else if (op == ">") ok = have > want;
        else if (op == "<") ok = have < want;
        else if (op == "<=") ok = have <= want;
        else if (op == "=" || op == "==") ok = have == want;
        else if (op == "^") ok = have >= want && have[0] == want[0];
        else if (op == "~") ok = have >= want && have[0] == want[0] && have[1] == want[1];
        else return false;
        if (!ok) return false;
    }
    return true;
}

ProjectConfig findProject(const fs::path &start, std::string &err) {
    ProjectConfig cfg;
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(start.empty() ? fs::current_path(ec) : start, ec);
    for (; !dir.empty(); dir = dir.parent_path()) {
        if (isFile(dir / "grav.toml")) break;
        if (dir == dir.root_path()) return cfg;
    }
    if (dir.empty()) return cfg;
    cfg.found = true;
    cfg.root = dir;
    std::ifstream in(dir / "grav.toml");
    std::string line, section;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t[0] == '[') { section = trim(t.substr(1, t.find(']') - 1)); continue; }
        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            err = (dir / "grav.toml").string() + ":" + std::to_string(lineNo) + ": expected key = value";
            return cfg;
        }
        std::string key = trim(t.substr(0, eq));
        if (section == "dependencies") {
            cfg.deps.push_back(parseDependency(key, t.substr(eq + 1)));
            continue;
        }
        auto values = quotedStrings(t.substr(eq + 1));
        if (key == "name" && !values.empty()) cfg.name = values[0];
        else if (key == "main" && !values.empty()) cfg.main = dir / values[0];
        else if (key == "lib" && !values.empty()) cfg.lib = dir / values[0];
        else if (key == "grav" && !values.empty()) cfg.grav = values[0];
        else if (key == "paths") for (const auto &v : values) cfg.paths.push_back(dir / v);
    }
    readLock(cfg);
    if (!cfg.grav.empty() && !versionSatisfies(GRAV_VERSION, cfg.grav)) {
        err = (dir / "grav.toml").string() + ": " + (cfg.name.empty() ? dir.filename().string() : cfg.name) +
              " needs grav " + cfg.grav + ", and this is grav " + GRAV_VERSION +
              " (update it with: grav self-update)";
    }
    return cfg;
}

fs::path currentExecutable() {
    std::error_code ec;
#ifdef __APPLE__
    char buf[4096];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) return {};
    fs::path p = fs::canonical(buf, ec);
    return ec ? fs::path(buf) : p;
#else
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : p;
#endif
}

fs::path stdLibDir() {
    if (const char *e = std::getenv("GRAV_STD"); e && *e && hasStd(e)) return e;
    if (const char *e = std::getenv("GRAV_HOME"); e && *e && hasStd(fs::path(e) / "lib"))
        return fs::path(e) / "lib";
    // Installed layouts: <prefix>/bin/grav with <prefix>/share/grav/lib or <prefix>/lib/grav.
    fs::path exe = currentExecutable();
    if (!exe.empty()) {
        fs::path prefix = exe.parent_path().parent_path();
        for (const fs::path &d : {prefix / "share" / "grav" / "lib", prefix / "lib" / "grav",
                                  prefix / "lib", exe.parent_path() / "lib"})
            if (hasStd(d)) return d;
    }
#ifdef GRAV_SOURCE_STD_DIR
    if (hasStd(GRAV_SOURCE_STD_DIR)) return GRAV_SOURCE_STD_DIR; // the tree gravc was built from
#endif
    return {};
}

const ProjectConfig &ModuleLoader::projectOf(const fs::path &dir) {
    std::error_code ec;
    std::string key = fs::weakly_canonical(dir, ec).string();
    for (const auto &[k, p] : projects_) if (k == key) return p;
    std::string err;
    ProjectConfig p = findProject(dir, err);
    if (!p.found) p = project_;
    projects_.push_back({key, p});
    return projects_.back().second;
}

// Walks the dependency graph from the program's project once: for each
// package name, the copy the whole program uses.
const std::map<std::string, fs::path> &ModuleLoader::chosenPackages() {
    if (chosenReady_) return chosen_;
    chosenReady_ = true;
    std::map<std::string, int> rank;                        // 0 the program's own, 1 local, 2 remote
    std::set<std::string> walked;
    std::vector<std::pair<ProjectConfig, int>> todo{{project_, 0}};
    std::error_code ec;
    while (!todo.empty()) {
        auto [proj, depth] = todo.back();
        todo.pop_back();
        for (const auto &dep : proj.deps) {
            fs::path dir = installedPackage(proj, dep);
            if (dir.empty()) continue;
            dir = fs::weakly_canonical(dir, ec);
            int r = depth == 0 ? 0 : (dep.local ? 1 : 2);
            auto it = rank.find(dep.name);
            if (it == rank.end() || r < it->second) { rank[dep.name] = r; chosen_[dep.name] = dir; }
            if (walked.insert(dir.string()).second) {
                std::string err;
                ProjectConfig sub = findProject(dir, err);
                if (sub.found && fs::weakly_canonical(sub.root, ec) == dir) {
                    if (!err.empty() && packageErr_.empty()) packageErr_ = err;
                    todo.push_back({sub, depth + 1});
                }
            }
        }
    }
    return chosen_;
}

// The entry file of an installed package: its grav.toml `lib`, else a
// conventional name.
static fs::path packageEntry(const fs::path &root, const std::string &name) {
    std::string err;
    ProjectConfig pc = findProject(root, err);
    if (pc.found && pc.root == fs::weakly_canonical(root) && !pc.lib.empty()) return pc.lib;
    for (const fs::path &c : {root / (name + ".grav"), root / "lib.grav", root / "src" / "lib.grav",
                              root / "mod.grav", root / "src" / (name + ".grav")})
        if (isFile(c)) return c;
    return root / "lib.grav";
}

fs::path ModuleLoader::resolve(const std::string &spec, const fs::path &fromDir,
                               std::vector<std::string> &tried) {
    auto attempt = [&](const fs::path &p) -> fs::path {
        tried.push_back(p.string());
        if (isFile(p)) return p;
        if (!p.has_extension()) {
            fs::path withExt = p;
            withExt += ".grav";
            if (isFile(withExt)) return withExt;
        }
        return {};
    };
    fs::path sp(spec);
    if (sp.is_absolute()) return attempt(sp);

    // `std` is the whole standard library; `std/<module>` one module of it.
    if (spec == "std" || spec.rfind("std/", 0) == 0) {
        fs::path dir = stdLibDir();
        if (dir.empty()) {
            tried.push_back("<standard library: not found; set GRAV_STD>");
            return {};
        }
        return attempt(spec == "std" ? dir / "std.grav" : dir / spec.substr(4));
    }
    if (fs::path p = attempt(fromDir / sp); !p.empty()) return p;
    // A dependency of the project this file belongs to: "name" or "name/sub/file".
    // The whole program uses one copy of each package (see chosenPackages()):
    // a local checkout replaces the one its dependents would fetch.
    std::string head = spec.substr(0, spec.find('/'));
    const ProjectConfig &owner = projectOf(fromDir);
    bool declared = false;
    for (const auto &dep : owner.deps) if (dep.name == head) declared = true;
    if (declared) {
        const auto &chosen = chosenPackages();
        if (auto it = chosen.find(head); it != chosen.end() && !it->second.empty()) {
            fs::path root = it->second;
            std::string r = root.string();
            if (std::find(packageRoots_.begin(), packageRoots_.end(), r) == packageRoots_.end()) packageRoots_.push_back(r);
            if (spec == head) return attempt(packageEntry(root, head));
            return attempt(root / spec.substr(head.size() + 1));
        }
    }
    for (const auto &dep : owner.deps) {
        if (dep.name != head) continue;
        fs::path root = installedPackage(owner, dep);
        if (!root.empty()) {
            std::error_code rec;
            std::string r = fs::weakly_canonical(root, rec).string();
            if (std::find(packageRoots_.begin(), packageRoots_.end(), r) == packageRoots_.end()) packageRoots_.push_back(r);
        }
        if (root.empty()) {
            tried.push_back(dep.local ? "<local package '" + dep.name + "': no directory " + (owner.root / dep.source).string() + ">"
                                      : "<package '" + dep.name + "' (" + dep.source + ") is not installed; run `grav install`>");
            return {};
        }
        if (spec == head) return attempt(packageEntry(root, head));
        return attempt(root / spec.substr(head.size() + 1));
    }
    for (const auto &root : owner.paths)
        if (fs::path p = attempt(root / sp); !p.empty()) return p;
    for (const auto &root : project_.paths)
        if (fs::path p = attempt(root / sp); !p.empty()) return p;
    if (const char *gp = std::getenv("GRAV_PATH"); gp && *gp) {
        std::string all = gp;
        size_t a = 0;
        while (a <= all.size()) {
            size_t b = all.find(':', a);
            if (b == std::string::npos) b = all.size();
            if (b > a)
                if (fs::path p = attempt(fs::path(all.substr(a, b - a)) / sp); !p.empty()) return p;
            a = b + 1;
        }
    }
    if (fs::path dir = stdLibDir(); !dir.empty())
        if (fs::path p = attempt(dir / sp); !p.empty()) return p;
    return {};
}

std::vector<std::string> ModuleLoader::whereIs(const std::string &name) const {
    std::vector<std::string> out;
    for (const auto &[file, mi] : info_)
        for (const auto &[n, ns] : mi.names)
            if (n == name && !ns.empty() && std::find(out.begin(), out.end(), ns) == out.end()) out.push_back(ns);
    return out;
}

std::string ModuleLoader::packageRootOf(const fs::path &p) const {
    std::error_code ec;
    std::string s = fs::weakly_canonical(p, ec).string(), best;
    for (const auto &r : packageRoots_)
        if (s.size() > r.size() && s.compare(0, r.size(), r) == 0 && s[r.size()] == '/' && r.size() > best.size()) best = r;
    return best;
}

bool ModuleLoader::isLibrary(const fs::path &p) const {
    static fs::path stdDir = [] {
        std::error_code ec;
        fs::path d = stdLibDir();
        return d.empty() ? d : fs::weakly_canonical(d, ec);
    }();
    if (stdDir.empty()) return false;
    std::error_code ec;
    std::string s = fs::weakly_canonical(p, ec).string(), d = stdDir.string();
    return s.size() > d.size() && s.compare(0, d.size(), d) == 0 && s[d.size()] == '/';
}

namespace {

// The words a file's code uses: identifiers outside comments and string text
// (but inside a string's `${...}`), leaving out `use` and `import` lines. Sets
// `hasC` when the file holds inline C (whose names it cannot see).
std::set<std::string> usedWords(const std::string &src, bool &hasC, bool &declares) {
    std::set<std::string> out;
    hasC = false;
    declares = false;
    size_t i = 0, n = src.size();
    auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    bool lineStart = true, skipLine = false;
    std::function<void(size_t &, char)> code; // (forward: strings hold code in ${})
    auto string = [&](size_t &k) {             // from just past the opening quote
        while (k < n && src[k] != '"' && src[k] != '\n') {
            if (src[k] == '\\') { k += 2; continue; }
            if (src[k] == '$' && k + 1 < n && src[k + 1] == '{') { k += 2; code(k, '}'); continue; }
            ++k;
        }
        if (k < n && src[k] == '"') ++k;
    };
    code = [&](size_t &k, char until) {
        int depth = 0;
        while (k < n) {
            char c = src[k];
            if (until && c == '{') ++depth;
            if (until && c == until && depth-- == 0) { ++k; return; }
            if (c == '\n') { lineStart = true; skipLine = false; ++k; continue; }
            if (c == ' ' || c == '\t' || c == '\r') { ++k; continue; }
            if (lineStart && !until) {
                lineStart = false;
                auto starts = [&](const char *w) { size_t l = strlen(w); return src.compare(k, l, w) == 0 && (k + l >= n || !ident(src[k + l])); };
                skipLine = starts("use") || starts("import");
                if (!skipLine && !starts("namespace") && c != '}' && c != '/' && c != '#') declares = true;
            }
            if (c == '/' && k + 1 < n && src[k + 1] == '/') { while (k < n && src[k] != '\n') ++k; continue; }
            if (c == '/' && k + 1 < n && src[k + 1] == '*') { size_t e = src.find("*/", k + 2); k = e == std::string::npos ? n : e + 2; continue; }
            if (c == '%' && k + 1 < n && src[k + 1] == '{') { hasC = true; size_t e = src.find("%}", k + 2); k = e == std::string::npos ? n : e + 2; continue; }
            if (src.compare(k, 3, "\"\"\"") == 0 || src.compare(k, 4, "r\"\"\"") == 0) {
                size_t q = src[k] == 'r' ? k + 1 : k;
                size_t e = src.find("\"\"\"", q + 3);
                k = e == std::string::npos ? n : e + 3;
                continue;
            }
            if (c == 'r' && k + 1 < n && src[k + 1] == '"' && (k == 0 || !ident(src[k - 1]))) { size_t e = src.find('"', k + 2); k = e == std::string::npos ? n : e + 1; continue; }
            if (c == '"') { ++k; string(k); continue; }
            if (c == '\'') { ++k; while (k < n && src[k] != '\'' && src[k] != '\n') k += src[k] == '\\' ? 2 : 1; if (k < n) ++k; continue; }
            if (ident(c)) {
                size_t s = k;
                while (k < n && ident(src[k])) ++k;
                if (!skipLine && !std::isdigit(static_cast<unsigned char>(src[s]))) out.insert(src.substr(s, k - s));
                continue;
            }
            ++k;
        }
    };
    code(i, 0);
    return out;
}

} // namespace

std::vector<ModuleLoader::Unused> ModuleLoader::unusedImports() const {
    std::vector<Unused> out;
    std::set<std::string> stdAll; // what bare `import "std"` brings
    if (fs::path dir = stdLibDir(); !dir.empty()) {
        std::error_code ec;
        std::string stdKey = fs::weakly_canonical(dir / "std.grav", ec).string();
        if (info_.count(stdKey)) for (const auto &f : closure(stdKey)) stdAll.insert(f);
    }
    for (const auto &[file, mi] : info_) {
        if (mi.importLines.empty() || isLibrary(file) || !packageRootOf(file).empty()) continue;
        std::ifstream in(file);
        if (!in) continue;
        std::stringstream buf;
        buf << in.rdbuf();
        bool hasC = false, declares = false;
        std::set<std::string> used = usedWords(buf.str(), hasC, declares);
        if (hasC || !declares) continue; // (inline C's names are unseen; a file of imports only re-exports)
        for (const auto &il : mi.importLines) {
            if (il.allowed || il.spec == "std") continue;
            bool isUsed = false, known = true;
            if (!il.alias.empty()) isUsed = used.count(il.alias) > 0;
            else if (!il.only.empty()) {
                for (const auto &[name, as] : il.only) isUsed = isUsed || used.count(as) > 0;
            } else {
                // What the import provides: a std module's files beyond bare std's; a
                // package's files and every package it brings (an umbrella package
                // re-exports its dependencies); a local file and the local files it imports.
                bool lib = isLibrary(il.key);
                std::string root = packageRootOf(il.key);
                size_t names = 0;
                for (const auto &f : closure(il.key)) {
                    bool provides = lib ? isLibrary(f) && !stdAll.count(f)
                                        : !isLibrary(f) && (!root.empty() || packageRootOf(f) == root);
                    if (!provides) continue;
                    auto it = info_.find(f);
                    if (it == info_.end()) continue;
                    for (const auto &[name, ns] : it->second.names) { ++names; isUsed = isUsed || used.count(name) > 0; }
                    for (const auto &m : it->second.macros) { ++names; isUsed = isUsed || used.count(m) > 0; }
                    if (isUsed) break;
                }
                known = names > 0; // (nothing to tell by: say nothing)
            }
            if (!isUsed && known)
                out.push_back({file, il.line, il.col, "unused import \"" + il.spec + "\" (nothing it declares is used here)"});
        }
    }
    return out;
}

std::vector<std::string> ModuleLoader::closure(const std::string &key) const {
    std::vector<std::string> order;
    std::set<std::string> seen;
    std::vector<std::string> stack{key};
    while (!stack.empty()) {
        std::string k = stack.back();
        stack.pop_back();
        if (!seen.insert(k).second) continue;
        order.push_back(k);
        auto it = info_.find(k);
        if (it == info_.end()) continue;
        for (auto i = it->second.imports.rbegin(); i != it->second.imports.rend(); ++i) stack.push_back(*i);
    }
    return order;
}

bool ModuleLoader::importUses(const std::string &key, const std::string &spec, const std::string &alias,
                              const std::vector<std::pair<std::string, std::string>> &only,
                              std::string &uses, std::string &err) const {
    auto files = closure(key);
    std::vector<std::string> spaces;
    for (const auto &f : files) {
        auto it = info_.find(f);
        if (it == info_.end()) continue;
        for (const auto &ns : it->second.namespaces)
            if (std::find(spaces.begin(), spaces.end(), ns) == spaces.end()) spaces.push_back(ns);
    }
    if (!only.empty()) { // import { A, B as C } from "..."
        for (const auto &[name, as] : only) {
            std::string ns;
            for (const auto &f : files) {
                auto it = info_.find(f);
                if (it == info_.end()) continue;
                for (const auto &[n, s] : it->second.names) if (n == name && ns.empty()) ns = s;
            }
            if (ns.empty()) {
                bool any = false;
                for (const auto &f : files) {
                    auto it = info_.find(f);
                    if (it == info_.end()) continue;
                    for (const auto &[n, s] : it->second.names) any = any || n == name;
                }
                if (!any) { err = "module '" + spec + "' has no '" + name + "'"; return false; }
                if (as != name) { err = "'" + name + "' is not in a namespace, so it cannot be renamed"; return false; }
                continue; // a global name: already visible
            }
            uses += "use " + ns + "." + name + (as != name ? " as " + as : "") + "; ";
        }
        return true;
    }
    if (!alias.empty()) { // import "..." as name
        std::string own;
        if (auto it = info_.find(key); it != info_.end() && !it->second.namespaces.empty())
            own = it->second.namespaces.front();
        if (own.empty() && spaces.size() == 1) own = spaces.front();
        if (own.empty()) { // an umbrella module: the namespace most of its imports share
            std::map<std::string, int> count;
            if (auto it = info_.find(key); it != info_.end())
                for (const auto &imp : it->second.imports)
                    if (auto jt = info_.find(imp); jt != info_.end() && !jt->second.namespaces.empty())
                        count[jt->second.namespaces.front()]++;
            int best = 0;
            for (const auto &[ns, n] : count) if (n > best) { best = n; own = ns; }
        }
        if (own.empty()) { err = "module '" + spec + "' declares no namespace to alias as '" + alias + "'"; return false; }
        uses += "use " + own + " as " + alias + "; ";
        return true;
    }
    // Plain `import "std/..."` from a program: bring the library's names into scope.
    if (isLibrary(key)) {
        for (const auto &ns : spaces) uses += "use " + ns + ".*; ";
    } else if (std::string root = packageRootOf(key); !root.empty()) {
        // A package's: the namespaces its own files declare (not the std ones it imports).
        std::vector<std::string> own;
        for (const auto &f : files) {
            if (packageRootOf(f) != root) continue;
            auto it = info_.find(f);
            if (it == info_.end()) continue;
            for (const auto &ns : it->second.namespaces)
                if (std::find(own.begin(), own.end(), ns) == own.end()) own.push_back(ns);
        }
        for (const auto &ns : own) uses += "use " + ns + ".*; ";
    }
    return true;
}

// Parses the forms of an import line:
//   import "spec"
//   import "spec" as name
//   import { A, B as C } from "spec"
static bool parseImport(const std::string &t, std::string &spec, std::string &alias,
                        std::vector<std::pair<std::string, std::string>> &only) {
    size_t i = 6;
    auto skip = [&] { while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i; };
    auto word = [&] {
        size_t s = i;
        while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '_')) ++i;
        return t.substr(s, i - s);
    };
    skip();
    if (i < t.size() && t[i] == '{') {
        ++i;
        for (;;) {
            skip();
            if (i < t.size() && t[i] == '}') { ++i; break; }
            std::string n = word();
            if (n.empty()) return false;
            skip();
            std::string as = n;
            if (t.compare(i, 2, "as") == 0 && (i + 2 >= t.size() || !std::isalnum(static_cast<unsigned char>(t[i + 2])))) {
                i += 2; skip(); as = word();
                if (as.empty()) return false;
                skip();
            }
            only.push_back({n, as});
            if (i < t.size() && t[i] == ',') { ++i; continue; }
            skip();
            if (i < t.size() && t[i] == '}') { ++i; break; }
            return false;
        }
        skip();
        if (word() != "from") return false;
        skip();
    }
    if (i >= t.size() || t[i] != '"') return false;
    size_t q2 = t.find('"', i + 1);
    if (q2 == std::string::npos) return false;
    spec = t.substr(i + 1, q2 - i - 1);
    i = q2 + 1;
    skip();
    if (t.compare(i, 2, "as") == 0) { i += 2; skip(); alias = word(); if (alias.empty()) return false; skip(); }
    if (i < t.size() && t[i] == ';') ++i;
    skip();
    return i >= t.size() || t.compare(i, 2, "//") == 0;
}

bool ModuleLoader::load(const fs::path &path, std::string &out, std::vector<SrcLoc> &srcMap,
                        std::string &err) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(path, ec);
    std::string key = ec ? path.string() : canon.string();
    if (visited_.count(key)) return true;
    visited_.insert(key);
    if (visited_.size() == 1) {   // the program itself: can this grav build every package it uses?
        chosenPackages();
        if (!packageErr_.empty()) { err = packageErr_; return false; }
    }
    bool library = isLibrary(key);
    // The standard library's prelude (its internal `use` lines) comes first.
    if (library && !preludeLoaded_) {
        preludeLoaded_ = true;
        fs::path prelude = stdLibDir() / "prelude.grav";
        if (isFile(prelude) && !load(prelude, out, srcMap, err)) return false;
    }

    std::ifstream file(path);
    if (!file) {
        err = "cannot open '" + path.string() + "'";
        return false;
    }
    ModInfo &mi = info_[key];
    std::string currentNs;
    std::string lineText;
    int lineNo = 0;
    while (std::getline(file, lineText)) {
        ++lineNo;
        std::string t = trim(lineText);
        // Remember namespaces and declarations (for `import { ... } from` and aliases).
        if (lineText.rfind("namespace ", 0) == 0) {
            size_t e = lineText.find_first_of(" {", 10);
            currentNs = trim(lineText.substr(10, e == std::string::npos ? std::string::npos : e - 10));
            if (std::find(mi.namespaces.begin(), mi.namespaces.end(), currentNs) == mi.namespaces.end())
                mi.namespaces.push_back(currentNs);
        } else if (lineText.rfind("}", 0) == 0) {
            currentNs.clear();
        } else if (!t.empty()) {
            std::string w = t;
            while (w.size() > 1 && w[0] == '@') { // `@Event class Welcomed`: past its decorators
                size_t e = 1;
                while (e < w.size() && (std::isalnum(static_cast<unsigned char>(w[e])) || w[e] == '_' || w[e] == '.')) ++e;
                if (e < w.size() && w[e] == '(') { int d = 0; for (; e < w.size(); ++e) { if (w[e] == '(') ++d; else if (w[e] == ')' && --d == 0) { ++e; break; } } }
                w = trim(w.substr(std::min(e, w.size())));
            }
            for (const char *m : {"export ", "final ", "abstract ", "async "})
                if (w.rfind(m, 0) == 0) w = trim(w.substr(std::string(m).size()));
            if (w.rfind("#define ", 0) == 0) {
                size_t s = 8, e = s;
                while (e < w.size() && (std::isalnum(static_cast<unsigned char>(w[e])) || w[e] == '_')) ++e;
                if (e > s) mi.macros.push_back(w.substr(s, e - s));
            }
            for (const char *kw : {"class ", "struct ", "enum ", "interface ", "trait ", "fn ", "type ", "const ", "let "}) {
                if (w.rfind(kw, 0) != 0) continue;
                size_t s = std::string(kw).size(), e = s;
                while (e < w.size() && (std::isalnum(static_cast<unsigned char>(w[e])) || w[e] == '_')) ++e;
                // Only declarations at the file's top level (or a namespace's first level).
                size_t indent = lineText.find_first_not_of(" \t");
                if (e > s && indent <= (currentNs.empty() ? 0u : 4u)) mi.names.push_back({w.substr(s, e - s), currentNs});
                break;
            }
        }
        if (t.rfind("import", 0) == 0 && (t.size() == 6 || t[6] == ' ' || t[6] == '\t' || t[6] == '{')) {
            std::string spec, alias;
            std::vector<std::pair<std::string, std::string>> only;
            if (!parseImport(t, spec, alias, only)) {
                err = path.string() + ":" + std::to_string(lineNo) +
                      ": malformed import; write import \"path\", import \"path\" as name, or "
                      "import { A, B as C } from \"path\"";
                return false;
            }
            std::vector<std::string> tried;
            fs::path found = resolve(spec, path.parent_path(), tried);
            if (found.empty()) {
                err = path.string() + ":" + std::to_string(lineNo) + ": cannot find module '" +
                      spec + "'; looked in:";
                for (const auto &p : tried) err += "\n    " + p;
                return false;
            }
            if (!load(found, out, srcMap, err)) return false;
            std::string foundKey = fs::weakly_canonical(found, ec).string();
            info_[key].imports.push_back(foundKey);
            {
                ModInfo::ImportLine il;
                il.spec = spec; il.alias = alias; il.key = foundKey; il.only = only; il.line = lineNo;
                il.col = static_cast<int>(lineText.find_first_not_of(" \t")) + 1;
                size_t cm = lineText.find("//", lineText.find('"', lineText.find('"') + 1));
                il.allowed = cm != std::string::npos && lineText.find("allow(unused)", cm) != std::string::npos;
                info_[key].importLines.push_back(il);
            }
            // A program importing the library (or asking for names/aliases) gets
            // `use` declarations in its own scope, on this same line.
            std::string uses;
            if (!library || !alias.empty() || !only.empty()) {
                std::string uerr;
                if (!importUses(foundKey, spec, alias, only, uses, uerr)) {
                    err = path.string() + ":" + std::to_string(lineNo) + ": " + uerr;
                    return false;
                }
            }
            out += uses + "\n";
            srcMap.push_back({path.string(), lineNo});
        } else {
            out += lineText;
            out += '\n';
            srcMap.push_back({path.string(), lineNo});
        }
    }
    return true;
}

} // namespace grav
