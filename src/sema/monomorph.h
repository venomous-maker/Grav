#ifndef GRAV_SEMA_MONOMORPH_H
#define GRAV_SEMA_MONOMORPH_H

#include <vector>

#include "ast/ast.h"
#include "common/diagnostics.h"

namespace grav {

// Expands generic structs and free functions into concrete, monomorphized copies
// and rewrites every `Name<Args>` reference (types, struct literals, turbofish
// calls) to its mangled instance. Runs after parsing and before symbol building,
// mutating the program in place: generic templates are removed and the needed
// instances are appended. Generic classes are reported as unsupported.
std::vector<GravError> monomorphize(Program &program);

} // namespace grav

#endif // GRAV_SEMA_MONOMORPH_H
