#ifndef BISTRA_TRANSFORMS_SIMPLIFY_H
#define BISTRA_TRANSFORMS_SIMPLIFY_H

#include <utility>

namespace bistra {

class Expr;
class Stmt;

/// Simplify the expression \p e.
/// \returns the current expression if it was not modified or a new simplified
/// expression.
Expr *simplifyExpr(Expr *e);

/// This is similar to LLVM's simplify demanded bits.
/// Updates the possible range in \p range.
/// \returns True if the range was computed;
bool computeKnownIntegerRange(Expr *e, std::pair<int, int> &range);

/// Simplify the program by eliminating dead code and simplifying the
/// program structure.
/// \returns true if the program was modified.
bool simplify(Stmt *s);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_SIMPLIFY_H
