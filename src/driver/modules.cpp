#include "driver/modules.h"

#include <cstdlib>
#include <fstream>
#include <unistd.h>

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
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == '[') continue; // comments, [sections]
        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            err = (dir / "grav.toml").string() + ":" + std::to_string(lineNo) + ": expected key = value";
            return cfg;
        }
        std::string key = trim(t.substr(0, eq));
        auto values = quotedStrings(t.substr(eq + 1));
        if (key == "name" && !values.empty()) cfg.name = values[0];
        else if (key == "main" && !values.empty()) cfg.main = dir / values[0];
        else if (key == "paths") for (const auto &v : values) cfg.paths.push_back(dir / v);
    }
    return cfg;
}

fs::path stdLibDir() {
    if (const char *e = std::getenv("GRAV_STD"); e && *e && hasStd(e)) return e;
    if (const char *e = std::getenv("GRAV_HOME"); e && *e && hasStd(fs::path(e) / "lib"))
        return fs::path(e) / "lib";
    // Installed layouts: <prefix>/bin/grav with <prefix>/share/grav/lib or <prefix>/lib/grav.
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
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

fs::path ModuleLoader::resolve(const std::string &spec, const fs::path &fromDir,
                               std::vector<std::string> &tried) const {
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

bool ModuleLoader::load(const fs::path &path, std::string &out, std::vector<SrcLoc> &srcMap,
                        std::string &err) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(path, ec);
    std::string key = ec ? path.string() : canon.string();
    if (visited_.count(key)) return true;
    visited_.insert(key);

    std::ifstream file(path);
    if (!file) {
        err = "cannot open '" + path.string() + "'";
        return false;
    }
    std::string lineText;
    int lineNo = 0;
    while (std::getline(file, lineText)) {
        ++lineNo;
        std::string t = trim(lineText);
        if (t.rfind("import", 0) == 0 && (t.size() == 6 || t[6] == ' ' || t[6] == '\t')) {
            auto q1 = t.find('"');
            auto q2 = q1 == std::string::npos ? q1 : t.find('"', q1 + 1);
            if (q2 == std::string::npos) {
                err = path.string() + ":" + std::to_string(lineNo) +
                      ": malformed import directive: " + lineText;
                return false;
            }
            std::string spec = t.substr(q1 + 1, q2 - q1 - 1);
            std::vector<std::string> tried;
            fs::path found = resolve(spec, path.parent_path(), tried);
            if (found.empty()) {
                err = path.string() + ":" + std::to_string(lineNo) + ": cannot find module '" +
                      spec + "'; looked in:";
                for (const auto &p : tried) err += "\n    " + p;
                return false;
            }
            if (!load(found, out, srcMap, err)) return false;
            out += "\n"; // keep imported decls separated
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
