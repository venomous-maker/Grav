#ifndef GRAV_DRIVER_INFER_CACHE_H
#define GRAV_DRIVER_INFER_CACHE_H

#include <string>

#include "ast/ast.h"
#include "sema/typechecker.h"

namespace grav {

// The generic type arguments the checker inferred (`mapOf("a", 1)` is
// `mapOf<string, int>`), kept between builds of a program. Each inference makes
// the front end run again, so a program whose code has many of them takes three
// passes; seeded with the last build's, it usually takes one.
//
// An inference depends only on the code of the function or class it is in and on
// the program's declarations (fields, signatures, globals, imports: everything
// outside function bodies). So last build's inferences are reused only when the
// declarations are unchanged (Program::surfaceHash), and then only those of
// functions and classes whose own text is unchanged (Program::scopes); their
// positions are kept relative to where the function or class starts, so edits
// elsewhere in the file do not lose them. Any other change re-infers as before.
namespace inferCache {

// The file kept for the program built from `inputPath` ("" when caching is off).
std::string pathFor(const std::string &inputPath, bool testMode);
// Puts the last build's inferences that still hold into `table`; how many.
size_t load(const std::string &path, const Program &program, InferenceTable &table);
// Keeps `table`'s inferences, made for `program`, for the next build.
void save(const std::string &path, const Program &program, const InferenceTable &table);

} // namespace inferCache
} // namespace grav

#endif // GRAV_DRIVER_INFER_CACHE_H
