#ifndef BISTRA_TRANSFORMS_SIMPLIFY_H
#define BISTRA_TRANSFORMS_SIMPLIFY_H

namespace bistra {

class Expr;
class Stmt;

/// Simplify the expression \p e.
/// \returns the current expression if it was not modified or a new simplified
/// expression.
Expr *simplifyExpr(Expr *e);

/// Simplify the program by eliminating dead code and simplifying the
/// program structure.
/// \returns true if the program was modified.
bool simplify(Stmt *s);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_SIMPLIFY_H
