#ifndef GRAV_CODEGEN_PRUNE_H
#define GRAV_CODEGEN_PRUNE_H

#include <string>

namespace grav {

// Removes the functions of the generated C that nothing can reach. A program's
// generic instances bring every method of their template (Array<User> gets sort,
// zip, chunk, ... and the sorter they use), and most are never called: in a large
// program about two thirds of the functions. The C compiler would drop them too,
// but only after reading and parsing them, which for a large program is a good
// part of the build.
//
// It works on the text: a function definition the code generator writes
// (`static`/`GRAV_FN ... name(...) {` up to its closing brace at the start of a
// line) is kept when its name is mentioned anywhere outside such definitions and
// prototypes (method tables, type info, reflection tables, macros, inline C,
// main), or in the body of a function that is kept. Anything mentioned by name
// is kept, so inline C that calls a generated function keeps it.
// Returns the C unchanged when it finds nothing to remove.
std::string pruneUnreachable(const std::string &c);

} // namespace grav

#endif // GRAV_CODEGEN_PRUNE_H
