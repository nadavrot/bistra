#ifndef BISTRA_TRANSFORMS_SIMPLIFY_H
#define BISTRA_TRANSFORMS_SIMPLIFY_H

namespace bistra {

class Expr;

/// Simplify the expression \p e.
/// \returns the current expression if it was not modified or a new simplified
/// expression.
Expr *simplifyExpr(Expr *e);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_SIMPLIFY_H
