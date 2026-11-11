// `grav add | install | remove | update | packages | new` — the package manager.
//
// Packages are git repositories (GitHub, GitLab, any git host, or a local path).
// Each version is cloned once into the shared cache, packagesDir()
// (~/.local/grav/packages/<host>/<owner>/<repo>@<version>), and projects refer to
// them from grav.toml:
//
//   [dependencies]
//   json5 = "github.com/someone/json5@v1.2.0"
//
// grav.lock records the commit each dependency resolved to, so `grav install` on
// another machine gets exactly the same code. A dependency can also be a local
// directory, `db = { path = "../grav-db" }`, used in place (handy before it is
// published). The standard library ships with the compiler and is never downloaded.
#include "driver/packages.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

#include "driver/modules.h"

namespace fs = std::filesystem;

namespace grav {

namespace {

std::string shellQuote(const std::string &s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

// Run a shell command, capturing stdout; returns the exit status.
int capture(const std::string &cmd, std::string &out) {
    out.clear();
    FILE *p = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!p) return -1;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    int rc = pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

int quiet(const std::string &cmd) { return std::system((cmd + " >/dev/null 2>&1").c_str()); }

// The URL git clones for a dependency source.
std::string cloneUrl(const std::string &source) {
    for (const char *p : {"https://", "http://", "ssh://", "git://", "file://", "git@", "/", "./", "../"})
        if (source.rfind(p, 0) == 0) return source;
    return "https://" + source;
}

// A package's default name: the repository name without ".git".
std::string defaultName(const std::string &source) {
    fs::path key = sourceKey(source);
    std::string n = key.filename().string();
    return n.empty() ? "package" : n;
}

// Semantic-version ordering for tags like v1.10.2 / 1.2.0-beta.
std::vector<long> versionParts(const std::string &tag) {
    std::vector<long> parts;
    size_t i = (tag.size() > 1 && (tag[0] == 'v' || tag[0] == 'V')) ? 1 : 0;
    while (i < tag.size()) {
        if (!std::isdigit(static_cast<unsigned char>(tag[i]))) return parts.size() >= 1 ? parts : std::vector<long>{};
        long v = 0;
        while (i < tag.size() && std::isdigit(static_cast<unsigned char>(tag[i]))) v = v * 10 + (tag[i++] - '0');
        parts.push_back(v);
        if (i < tag.size() && tag[i] == '.') ++i;
        else break;
    }
    return parts;
}

// The newest release tag of a repository ("" when it has none).
std::string newestTag(const std::string &url) {
    std::string out;
    if (capture("git ls-remote --tags --refs " + shellQuote(url), out) != 0) return "";
    std::string best;
    std::vector<long> bestV;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        size_t p = line.find("refs/tags/");
        if (p == std::string::npos) continue;
        std::string tag = line.substr(p + 10);
        auto v = versionParts(tag);
        if (v.empty()) continue;
        if (best.empty() || v > bestV) { best = tag; bestV = v; }
    }
    return best;
}

// Clone `url` at `version` (tag/branch; "" = default branch) or at `commit`
// into the cache. Returns the directory, or empty with `err` set.
fs::path fetch(const std::string &source, const std::string &version, const std::string &commit,
               std::string &resolvedCommit, std::string &err) {
    std::string url = cloneUrl(source);
    std::error_code ec;
    // Already cached?
    std::string label = !version.empty() ? version : (commit.empty() ? "" : commit.substr(0, 12));
    if (!label.empty()) {
        fs::path dir = packagePath(source, label);
        if (fs::is_directory(dir / ".git", ec) || fs::is_directory(dir, ec)) {
            std::string c;
            capture("git -C " + shellQuote(dir.string()) + " rev-parse HEAD", c);
            if (commit.empty() || c == commit) { resolvedCommit = c; return dir; }
        }
    }
    fs::path tmp = packagesDir() / (".tmp-" + std::to_string(getpid()));
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp.parent_path(), ec);
    std::string cmd = "git clone --quiet ";
    if (commit.empty()) cmd += "--depth 1 ";
    if (!version.empty() && commit.empty()) cmd += "--branch " + shellQuote(version) + " ";
    cmd += shellQuote(url) + " " + shellQuote(tmp.string());
    std::cerr << "grav: fetching " << source << (version.empty() ? "" : "@" + version) << "\n";
    if (quiet(cmd) != 0) {
        fs::remove_all(tmp, ec);
        err = "cannot clone " + url + (version.empty() ? "" : " at '" + version + "'") +
              " (is the repository public and the version a tag or branch?)";
        return {};
    }
    if (!commit.empty() && quiet("git -C " + shellQuote(tmp.string()) + " checkout --quiet " + shellQuote(commit)) != 0) {
        fs::remove_all(tmp, ec);
        err = "commit " + commit + " (from grav.lock) is not in " + url;
        return {};
    }
    capture("git -C " + shellQuote(tmp.string()) + " rev-parse HEAD", resolvedCommit);
    if (label.empty()) label = resolvedCommit.substr(0, 12);
    fs::path dir = packagePath(source, label);
    fs::create_directories(dir.parent_path(), ec);
    fs::remove_all(dir, ec);
    fs::rename(tmp, dir, ec);
    if (ec) { err = "cannot move the package into " + dir.string() + ": " + ec.message(); return {}; }
    return dir;
}

// ---- grav.toml / grav.lock editing -----------------------------------------

std::vector<std::string> readLines(const fs::path &p) {
    std::vector<std::string> lines;
    std::ifstream in(p);
    std::string l;
    while (std::getline(in, l)) lines.push_back(l);
    return lines;
}

void writeLines(const fs::path &p, const std::vector<std::string> &lines) {
    std::ofstream out(p);
    for (const auto &l : lines) out << l << "\n";
}

std::string trimmed(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Set (or with an empty value, remove) `name` in grav.toml's [dependencies].
void setDependency(const fs::path &toml, const std::string &name, const std::string &value) {
    auto lines = readLines(toml);
    int start = -1, end = static_cast<int>(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string t = trimmed(lines[i]);
        if (t == "[dependencies]") start = static_cast<int>(i);
        else if (start >= 0 && !t.empty() && t[0] == '[') { end = static_cast<int>(i); break; }
    }
    // A value already written as an inline table (`{ path = "..." }`) goes in as it is.
    std::string entry = name + " = " + (!value.empty() && value[0] == '{' ? value : "\"" + value + "\"");
    if (start < 0) {
        if (value.empty()) return;
        if (!lines.empty() && !trimmed(lines.back()).empty()) lines.push_back("");
        lines.push_back("[dependencies]");
        lines.push_back(entry);
        writeLines(toml, lines);
        return;
    }
    for (int i = start + 1; i < end; ++i) {
        std::string t = trimmed(lines[i]);
        size_t eq = t.find('=');
        if (eq == std::string::npos || trimmed(t.substr(0, eq)) != name) continue;
        if (value.empty()) lines.erase(lines.begin() + i);
        else lines[i] = entry;
        writeLines(toml, lines);
        return;
    }
    if (value.empty()) return;
    int at = end;
    while (at > start + 1 && trimmed(lines[at - 1]).empty()) --at;
    lines.insert(lines.begin() + at, entry);
    writeLines(toml, lines);
}

void writeLock(const fs::path &root, const std::vector<std::pair<std::string, LockEntry>> &lock) {
    std::ofstream out(root / "grav.lock");
    out << "# grav.lock: exact commits of the installed dependencies (generated; commit this file)\n";
    for (const auto &[name, e] : lock)
        out << name << " = { source = \"" << e.source << "\", version = \"" << e.version
            << "\", commit = \"" << e.commit << "\" }\n";
}

void putLock(std::vector<std::pair<std::string, LockEntry>> &lock, const std::string &name, const LockEntry &e) {
    for (auto &[n, x] : lock) if (n == name) { x = e; return; }
    lock.push_back({name, e});
}

ProjectConfig requireProject(bool create, std::string &err) {
    ProjectConfig p = findProject(fs::current_path(), err);
    if (p.found || !create) return p;
    fs::path toml = fs::current_path() / "grav.toml";
    std::ofstream out(toml);
    out << "name = \"" << fs::current_path().filename().string() << "\"\n";
    out.close();
    std::cerr << "grav: created " << toml.string() << "\n";
    return findProject(fs::current_path(), err);
}

// Install one dependency (and, recursively, the dependencies of the package).
bool installDep(const ProjectConfig &project, const Dependency &dep, bool useLock,
                std::vector<std::pair<std::string, LockEntry>> &lock, std::set<std::string> &seen,
                std::string &err) {
    if (dep.local) {                                            // nothing to fetch: check it, then its dependencies
        fs::path dir = installedPackage(project, dep);
        if (dir.empty()) {
            err = "the local package '" + dep.name + "' is not at " + (project.root / dep.source).string();
            return false;
        }
        if (seen.count(dir.string())) return true;
        seen.insert(dir.string());
        lock.erase(std::remove_if(lock.begin(), lock.end(), [&](const auto &p) { return p.first == dep.name; }), lock.end());
        std::cerr << "grav: " << dep.name << " (local) -> " << dir.string() << "\n";
        std::string e2;
        ProjectConfig sub = findProject(dir, e2);
        if (sub.found && fs::weakly_canonical(sub.root) == fs::weakly_canonical(dir))
            for (const auto &d : sub.deps)
                if (!installDep(sub, d, true, lock, seen, err)) return false;
        return true;
    }
    if (seen.count(dep.source)) return true;
    seen.insert(dep.source);
    const LockEntry *l = useLock ? project.locked(dep.name) : nullptr;
    if (l && l->source != dep.source) l = nullptr;
    std::string version = dep.version, commit = l ? l->commit : "";
    if (l && version.empty()) version = l->version;
    if (version.empty() && commit.empty()) version = newestTag(cloneUrl(dep.source));
    std::string resolved;
    fs::path dir = fetch(dep.source, version, commit, resolved, err);
    if (dir.empty()) return false;
    putLock(lock, dep.name, LockEntry{dep.source, version, resolved});
    std::cerr << "grav: " << dep.name << " " << (version.empty() ? resolved.substr(0, 12) : version)
              << " -> " << dir.string() << "\n";
    // Its own dependencies.
    std::string e2;
    ProjectConfig sub = findProject(dir, e2);
    if (sub.found && fs::weakly_canonical(sub.root) == fs::weakly_canonical(dir))
        for (const auto &d : sub.deps)
            if (!installDep(sub, d, true, lock, seen, err)) return false;
    return true;
}

int usagePkg() {
    std::cerr << "usage:\n"
                 "  grav new <name>                 create a project (grav.toml, src/main.grav)\n"
                 "  grav add <source>[@version] [--name n]   add a dependency and install it\n"
                 "        source: github.com/owner/repo, gitlab.com/group/repo, a git URL, or a path\n"
                 "  grav add <dir> [--name n]        use a package from a local directory as it is\n"
                 "        (--git clones a local repository's commits instead)\n"
                 "  grav install                    install every dependency (exact commits from grav.lock)\n"
                 "  grav update [name]              move dependencies to their newest tag\n"
                 "  grav remove <name>              drop a dependency\n"
                 "  grav packages                   list dependencies and where they are installed\n";
    return 2;
}

} // namespace

bool isPackageCommand(const std::string &cmd) {
    return cmd == "add" || cmd == "install" || cmd == "remove" || cmd == "update" ||
           cmd == "packages" || cmd == "new";
}

int runPackageCommand(const std::vector<std::string> &args) {
    if (args.empty()) return usagePkg();
    const std::string &cmd = args[0];
    std::string err;

    if (cmd == "new") {
        if (args.size() < 2) return usagePkg();
        fs::path dir = args[1];
        std::error_code ec;
        if (fs::exists(dir / "grav.toml", ec)) { std::cerr << "grav: " << dir.string() << " already has a grav.toml\n"; return 1; }
        fs::create_directories(dir / "src", ec);
        fs::create_directories(dir / "tests", ec);
        std::ofstream(dir / "grav.toml") << "name = \"" << dir.filename().string() << "\"\n"
                                          << "main = \"src/main.grav\"\n\n[dependencies]\n";
        std::ofstream(dir / "src" / "main.grav")
            << "import \"std\"\n\nfn greeting(name: string) -> string { return \"hello, \" + name }\n\n"
               "fn main() {\n    print(greeting(\"" << dir.filename().string() << "\"))\n}\n\n"
               "test \"greeting\" {\n    assertEq(greeting(\"grav\"), \"hello, grav\")\n}\n";
        std::ofstream(dir / ".gitignore") << "/src/main\n*.gen.c\n";
        std::cerr << "grav: created " << dir.string() << " (try: cd " << dir.string() << " && grav run)\n";
        return 0;
    }

    if (cmd == "add") {
        if (args.size() < 2) return usagePkg();
        std::string spec, name;
        bool asPath = false, asGit = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--name" && i + 1 < args.size()) name = args[++i];
            else if (args[i] == "--path") asPath = true;
            else if (args[i] == "--git") asGit = true;
            else if (spec.empty()) spec = args[i];
        }
        if (spec.empty()) return usagePkg();
        std::error_code dec;
        // A directory on this machine is used in place (edits show at once);
        // `--git` clones a local repository's commits instead.
        if (!asGit && fs::is_directory(spec, dec)) asPath = true;
        ProjectConfig project = requireProject(true, err);
        if (!err.empty()) { std::cerr << "grav: " << err << "\n"; return 1; }
        if (asPath) {
            fs::path abs = fs::weakly_canonical(fs::absolute(spec, dec), dec);
            if (!fs::is_directory(abs, dec)) { std::cerr << "grav: no directory " << spec << "\n"; return 1; }
            Dependency dep;
            dep.local = true;
            // Relative to grav.toml (so the pair can move together), unless that path
            // climbs all the way to the filesystem root: then the absolute one.
            fs::path root = fs::weakly_canonical(project.root, dec);
            fs::path rel = fs::relative(abs, root, dec);
            size_t up = 0, depth = 0;
            for (const auto &part : rel) if (part == "..") ++up;
            for (const auto &part : root.relative_path()) { (void)part; ++depth; }
            dep.source = rel.empty() || up >= depth ? abs.generic_string() : rel.generic_string();
            dep.name = name.empty() ? abs.filename().string() : name;
            auto lock = project.lock;
            std::set<std::string> seen;
            if (!installDep(project, dep, false, lock, seen, err)) { std::cerr << "grav: " << err << "\n"; return 1; }
            setDependency(project.root / "grav.toml", dep.name, "{ path = \"" + dep.source + "\" }");
            writeLock(project.root, lock);
            std::cerr << "grav: added " << dep.name << " from " << dep.source << " (import \"" << dep.name << "\")\n";
            return 0;
        }
        Dependency dep = parseDependency(name, "\"" + spec + "\"");
        if (dep.name.empty()) dep.name = defaultName(dep.source);
        auto lock = project.lock;
        std::set<std::string> seen;
        if (!installDep(project, dep, false, lock, seen, err)) { std::cerr << "grav: " << err << "\n"; return 1; }
        std::string version;
        for (const auto &[n, e] : lock) if (n == dep.name) version = e.version;
        setDependency(project.root / "grav.toml", dep.name, dep.source + (version.empty() ? "" : "@" + version));
        writeLock(project.root, lock);
        std::cerr << "grav: added " << dep.name << " (import \"" << dep.name << "\")\n";
        return 0;
    }

    ProjectConfig project = requireProject(false, err);
    if (!project.found) { std::cerr << "grav: no grav.toml here or in a parent directory (create one with `grav new` or `grav add`)\n"; return 1; }

    if (cmd == "install" || cmd == "update") {
        auto lock = project.lock;
        std::set<std::string> seen;
        bool any = false;
        for (Dependency dep : project.deps) {
            bool target = cmd == "install" || args.size() < 2 || args[1] == dep.name;
            if (!target) continue;
            any = true;
            bool useLock = cmd == "install";
            if (cmd == "update") dep.version = ""; // newest tag
            if (!installDep(project, dep, useLock, lock, seen, err)) { std::cerr << "grav: " << err << "\n"; return 1; }
            if (cmd == "update" && !dep.local) {
                for (const auto &[n, e] : lock)
                    if (n == dep.name)
                        setDependency(project.root / "grav.toml", dep.name, dep.source + (e.version.empty() ? "" : "@" + e.version));
            }
        }
        if (!any && args.size() >= 2) { std::cerr << "grav: no dependency named '" << args[1] << "'\n"; return 1; }
        writeLock(project.root, lock);
        if (project.deps.empty()) std::cerr << "grav: no dependencies in grav.toml\n";
        return 0;
    }

    if (cmd == "remove") {
        if (args.size() < 2) return usagePkg();
        bool found = false;
        for (const auto &d : project.deps) found = found || d.name == args[1];
        if (!found) { std::cerr << "grav: no dependency named '" << args[1] << "'\n"; return 1; }
        setDependency(project.root / "grav.toml", args[1], "");
        auto lock = project.lock;
        lock.erase(std::remove_if(lock.begin(), lock.end(), [&](const auto &p) { return p.first == args[1]; }), lock.end());
        writeLock(project.root, lock);
        std::cerr << "grav: removed " << args[1] << " (the cached copy stays in " << packagesDir().string() << ")\n";
        return 0;
    }

    if (cmd == "packages") {
        if (project.deps.empty()) { std::cout << "no dependencies\n"; return 0; }
        for (const auto &d : project.deps) {
            fs::path where = installedPackage(project, d);
            const LockEntry *l = project.locked(d.name);
            if (d.local) {
                std::cout << d.name << "  path " << d.source << "  (local)\n    "
                          << (where.empty() ? "missing: no directory there" : where.string()) << "\n";
                continue;
            }
            std::cout << d.name << "  " << d.source << (d.version.empty() ? "" : "@" + d.version)
                      << (l ? "  (" + l->commit.substr(0, 12) + ")" : "") << "\n    "
                      << (where.empty() ? "not installed: run `grav install`" : where.string()) << "\n";
        }
        return 0;
    }
    return usagePkg();
}

} // namespace grav
