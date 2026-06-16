#ifndef GRAV_LEXER_PREPROCESS_H
#define GRAV_LEXER_PREPROCESS_H

#include <vector>

#include "lexer/token.h"

namespace grav {

// A token-level macro pass run between the lexer and the parser. It handles
// `#define` directives (object-like `#define NAME tokens` and function-like
// `#define NAME(a, b) tokens`) and expands their uses, with a recursion guard.
// Directives occupy a single source line (like C) and leave no trace in output.
// Throws GravError on a malformed directive or argument list.
std::vector<Token> preprocess(std::vector<Token> tokens);

} // namespace grav

#endif // GRAV_LEXER_PREPROCESS_H
