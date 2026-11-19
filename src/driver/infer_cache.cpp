#include "driver/infer_cache.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "driver/build_cache.h"

namespace fs = std::filesystem;

namespace grav {
namespace inferCache {

namespace {

// Format, one record per line:
//   surface <hash>
//   e <scope> <checked> <hash> <line in it> <col> <name> <args>     an inference
// (`checked` is the narrowest scope holding it: a method of a class, or the scope)
//   m <Instance.method>                                            a method made on demand
// Names are written with their length first, so any character may be in them.
void putName(std::ostream &o, const std::string &s) { o << s.size() << ':' << s; }
bool getName(std::istream &in, std::string &s) {
    size_t n = 0;
    char colon = 0;
    if (!(in >> n) || !in.get(colon) || colon != ':') return false;
    s.assign(n, '\0');
    return static_cast<bool>(in.read(&s[0], static_cast<std::streamsize>(n))) || n == 0;
}

void putType(std::ostream &o, const TypeRef &t) {
    o << ' ' << static_cast<int>(t.kind) << ' ' << t.bits << ' ' << t.isUnsigned << ' ' << t.arrayLen << ' ';
    putName(o, t.name);
    o << ' ' << t.args.size();
    for (const auto &a : t.args) putType(o, a);
    o << ' ' << t.fnParams.size();
    for (const auto &p : t.fnParams) putType(o, p);
    o << ' ' << (t.elem ? 1 : 0);
    if (t.elem) putType(o, *t.elem);
}

bool getType(std::istream &in, TypeRef &t, int depth = 0) {
    if (depth > 64) return false;
    int kind = 0, uns = 0;
    size_t n = 0;
    if (!(in >> kind >> t.bits >> uns >> t.arrayLen)) return false;
    if (kind < 0 || kind > static_cast<int>(TypeRef::Kind::Error)) return false;
    t.kind = static_cast<TypeRef::Kind>(kind);
    t.isUnsigned = uns != 0;
    if (!getName(in, t.name) || !(in >> n) || n > 64) return false;
    t.args.resize(n);
    for (auto &a : t.args) if (!getType(in, a, depth + 1)) return false;
    if (!(in >> n) || n > 64) return false;
    t.fnParams.resize(n);
    for (auto &p : t.fnParams) if (!getType(in, p, depth + 1)) return false;
    int hasElem = 0;
    if (!(in >> hasElem)) return false;
    if (hasElem) {
        TypeRef e;
        if (!getType(in, e, depth + 1)) return false;
        t.elem = std::make_shared<TypeRef>(std::move(e));
    }
    return true;
}

// "scope|line:col:name" (see siteKey) into its parts.
bool splitKey(const std::string &key, std::string &scope, int &line, int &col, std::string &name) {
    size_t bar = key.find('|');
    if (bar == std::string::npos) return false;
    size_t c1 = key.find(':', bar + 1);
    size_t c2 = c1 == std::string::npos ? c1 : key.find(':', c1 + 1);
    if (c2 == std::string::npos) return false;
    scope = key.substr(0, bar);
    try {
        line = std::stoi(key.substr(bar + 1, c1 - bar - 1));
        col = std::stoi(key.substr(c1 + 1, c2 - c1 - 1));
    } catch (...) {
        return false;
    }
    name = key.substr(c2 + 1);
    return true;
}

} // namespace

std::string pathFor(const std::string &inputPath, bool testMode) {
    std::string dir = buildCacheDir("infer");
    if (dir.empty()) return "";
    std::error_code ec;
    std::string key = fs::absolute(inputPath, ec).string() + (testMode ? "\ntest" : "");
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char ch : key) h = (h ^ ch) * 1099511628211ULL;
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", h);
    return (fs::path(dir) / hex).string();
}

size_t load(const std::string &path, const Program &program, InferenceTable &table) {
    if (path.empty()) return 0;
    std::ifstream in(path);
    std::string tag;
    unsigned long long surface = 0;
    if (!(in >> tag >> surface) || tag != "surface" || surface != program.surfaceHash) return 0;
    size_t used = 0;
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        std::istringstream rec(line);
        if (!(rec >> tag)) continue;
        if (tag == "m") {
            std::string key;
            if (getName(rec >> std::ws, key)) table.methods.insert(key);
            continue;
        }
        if (tag != "e") continue;
        std::string scope, checked, name;
        unsigned long long scopeHash = 0;
        int rel = 0, col = 0;
        size_t n = 0;
        if (!getName(rec >> std::ws, scope) || !getName(rec >> std::ws, checked) || !(rec >> scopeHash >> rel >> col) ||
            !getName(rec >> std::ws, name) || !(rec >> n) || n > 64)
            continue;
        auto it = program.scopes.find(checked);
        if (it == program.scopes.end() || it->second.hash != scopeHash) continue;
        std::vector<TypeRef> args(n);
        bool ok = true;
        for (auto &a : args) ok = ok && getType(rec, a);
        if (!ok) continue;
        table.entries[scope + "|" + std::to_string(it->second.line + rel) + ":" + std::to_string(col) + ":" + name] =
            std::move(args);
        ++used;
    }
    return used;
}

void save(const std::string &path, const Program &program, const InferenceTable &table) {
    if (path.empty()) return;
    std::ostringstream o;
    o << "surface " << program.surfaceHash << "\n";
    for (const auto &[key, args] : table.entries) {
        std::string scope, name;
        int line = 0, col = 0;
        if (!splitKey(key, scope, line, col, name)) continue;
        std::string checked = scope;
        auto ms = program.members.find(scope);
        if (ms != program.members.end())
            for (const auto &m : ms->second)
                if (line >= m.firstLine && line <= m.lastLine) { checked = m.key; break; }
        auto it = program.scopes.find(checked);
        if (it == program.scopes.end()) continue; // (inside a generic instance: not kept)
        o << "e ";
        putName(o, scope);
        o << ' ';
        putName(o, checked);
        o << ' ' << it->second.hash << ' ' << (line - it->second.line) << ' ' << col << ' ';
        putName(o, name);
        o << ' ' << args.size();
        for (const auto &a : args) putType(o, a);
        o << "\n";
    }
    for (const auto &m : table.methods) {
        o << "m ";
        putName(o, m);
        o << "\n";
    }
    // Written aside and renamed, so a build reading it never sees half a file.
    std::string tmp = path + ".tmp" + std::to_string(::getpid());
    {
        std::ofstream out(tmp);
        if (!out) return;
        out << o.str();
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp, ec);
}

} // namespace inferCache
} // namespace grav
