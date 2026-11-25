// `grav lint --fix`: applying the lints' mechanical fixes to source files, with
// the originals kept aside so a round that stops the program compiling can be
// undone.
#ifndef GRAV_DRIVER_LINTFIX_H
#define GRAV_DRIVER_LINTFIX_H

#include <string>
#include <vector>

namespace grav {

// One edit in one file: replace bytes from (line, col) to just before (endLine,
// endCol) with `text` (1-based; columns count bytes); or delete `line` whole.
struct LintEdit {
    std::string file;
    int line = 0, col = 0, endLine = 0, endCol = 0;
    std::string text;
    bool deleteLine = false;
    std::string lint;   // its name, for the report
};

// Applies the edits that do not overlap (the first of overlapping ones wins;
// the rest wait for the next round), keeping each file's original in
// `backupDir` the first time it changes. Returns how many were applied; each
// applied one is described on `report` lines ("file:line: lint").
int applyLintEdits(std::vector<LintEdit> edits, const std::string &backupDir, std::vector<std::string> &report);

// Puts back every file kept in `backupDir` (a round that broke the program).
int restoreLintBackups(const std::string &backupDir);

} // namespace grav

#endif // GRAV_DRIVER_LINTFIX_H
