#ifndef GRAV_SEMA_CELLS_H
#define GRAV_SEMA_CELLS_H

#include "ast/ast.h"

namespace grav {

// Lets lambdas assign the variables they capture. Captures are copies, so a
// local (or parameter, loop or catch variable) that some lambda assigns is moved
// into a shared heap cell instead: its declaration becomes
// `let n = new __GravCell(init)`, and every use of it, in the function and in its
// lambdas, reads and writes `n.value`. The lambdas then capture the cell, so they
// and the function see one variable. Runs on the parsed program, before
// monomorphization and checking; variables no lambda assigns are untouched.
void boxAssignedCaptures(Program &program);

} // namespace grav

#endif // GRAV_SEMA_CELLS_H
