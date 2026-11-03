// Data enums after parsing: `Shape.Circle(2.0)` and `Shape.Empty` become
// `new Shape$Circle(2.0)` and `new Shape$Empty()` (see parser/parser_enum.cpp
// for how the enum itself is lowered to classes).
#ifndef GRAV_SEMA_ENUMS_H
#define GRAV_SEMA_ENUMS_H

#include "ast/ast.h"

namespace grav {

void lowerEnumConstructors(Program &program);

} // namespace grav

#endif // GRAV_SEMA_ENUMS_H
