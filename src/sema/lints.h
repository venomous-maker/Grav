// Lints: code that compiles but is probably a mistake, or that has a plainer
// way to be written, found on the type-checked program (like Rust's clippy).
// Each is a warning whose message ends in ` [lint-name]`, optionally followed by
// kLintHelp and a suggestion. `// allow(lint-name)` (or `allow(lints)`) on the
// line keeps one quiet; the driver reports them for the program's own files.
#ifndef GRAV_SEMA_LINTS_H
#define GRAV_SEMA_LINTS_H

#include <vector>

#include "ast/ast.h"
#include "common/diagnostics.h"

namespace grav {

// Separates a lint's message from its suggestion ("help: ...").
constexpr char kLintHelp = '\x1f';

std::vector<GravError> lintProgram(Program &program);

} // namespace grav

#endif // GRAV_SEMA_LINTS_H
