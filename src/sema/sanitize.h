#ifndef GRAV_SEMA_SANITIZE_H
#define GRAV_SEMA_SANITIZE_H

#include "ast/ast.h"

namespace grav {

// Renames local identifiers (variables, parameters, loop and catch variables,
// lambda parameters/captures) that would collide with C keywords or with C
// library names the generated code calls (e.g. a parameter named `short` or a
// local named `puts`), so any valid Grav program compiles to valid C. Runs
// after type checking; references are renamed consistently.
void sanitizeIdentifiers(Program &program);

// The C-safe spelling of a local identifier.
std::string safeLocalName(const std::string &name);

} // namespace grav

#endif // GRAV_SEMA_SANITIZE_H
