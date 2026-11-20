// `grav self-update`. install.sh leaves a record next to the standard library
// (PREFIX/share/grav/install.env) naming the source checkout it built; this
// runs that checkout's install.sh with --update, which pulls, rebuilds and
// reinstalls only when something changed. The process is replaced by the
// script, so the binary being updated is no longer running.
#include "driver/self_update.h"
#include "driver/modules.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>

namespace fs = std::filesystem;

namespace grav {

namespace {

// KEY='value' lines (the record is written by install.sh, one per line).
std::map<std::string, std::string> readRecord(const fs::path &file) {
    std::map<std::string, std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        std::string v = line.substr(eq + 1);
        if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'') v = v.substr(1, v.size() - 2);
        out[line.substr(0, eq)] = v;
    }
    return out;
}

int usage() {
    std::cerr << "usage: grav self-update [--check] [--force] [--ref <branch|tag>]\n"
                 "  --check   only say whether an update is available\n"
                 "  --force   rebuild and reinstall even when nothing changed\n"
                 "  --ref     follow from now on: latest (the newest release), a tag, or a branch\n";
    return 2;
}

} // namespace

int runSelfUpdate(const std::vector<std::string> &args) {
    std::vector<std::string> pass;
    for (size_t i = 0; i < args.size(); i++) {
        const std::string &a = args[i];
        if (a == "--check" || a == "--force") pass.push_back(a);
        else if (a == "--ref" && i + 1 < args.size()) { pass.push_back(a); pass.push_back(args[++i]); }
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else return usage();
    }

    fs::path exe = currentExecutable();
    if (exe.empty()) {
        std::cerr << "grav self-update: cannot tell where this grav is installed\n";
        return 1;
    }
    fs::path prefix = exe.parent_path().parent_path();
    fs::path record = prefix / "share" / "grav" / "install.env";
    if (!fs::exists(record)) {
        std::cerr << "grav self-update: this grav (" << exe.string() << ") was not installed by install.sh,\n"
                  << "so there is nothing recorded to update from (no " << record.string() << ").\n"
                  << "Install it with install.sh to get updates, or rebuild your checkout yourself.\n";
        return 1;
    }
    auto rec = readRecord(record);
    fs::path script = fs::path(rec["GRAV_SRC"]) / "install.sh";
    if (rec["GRAV_SRC"].empty() || !fs::exists(script)) {
        std::cerr << "grav self-update: the source checkout grav was built from is gone ("
                  << (rec["GRAV_SRC"].empty() ? "not recorded" : rec["GRAV_SRC"]) << ").\n"
                  << "Run install.sh again to reinstall.\n";
        return 1;
    }

    std::vector<std::string> argvS = {"bash", script.string(), "--update", "--prefix", prefix.string()};
    argvS.insert(argvS.end(), pass.begin(), pass.end());
    std::vector<char *> argvC;
    for (auto &s : argvS) argvC.push_back(s.data());
    argvC.push_back(nullptr);
    std::cout.flush();
    execvp("bash", argvC.data());
    std::cerr << "grav self-update: cannot run bash\n";
    return 1;
}

} // namespace grav
