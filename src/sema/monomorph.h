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
struct InferenceTable;
// `inferred`: type arguments the checker inferred in an earlier pass, for calls
// inside generic instances (the rest are applied to the parsed program).
std::vector<GravError> monomorphize(Program &program, const InferenceTable *inferred = nullptr);

// A deep copy of an expression (for rewrites such as `obj[k] += v`).
ExprPtr cloneExpression(const Expr *e);

} // namespace grav

#endif // GRAV_SEMA_MONOMORPH_H
