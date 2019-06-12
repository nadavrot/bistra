#ifndef BISTRA_TRANSFORMS_DEPENDENCE_H
#define BISTRA_TRANSFORMS_DEPENDENCE_H

#include "bistra/Program/Program.h"
#include "bistra/Program/Types.h"

namespace bistra {

/// Maps kind of dependencies:
enum class DepRelationKind {
  SomeDep, // "<"  or ">"
  Equals,  // "="
  NoDep,   // None.
};

/// \returns True if the first subscript depends on the second subscript for the
/// inices \p I1 and \p I2, and arguments \p A1, and \p A2, for the indices
/// \p indices1, \p indices2.
DepRelationKind
checkWeakSIVDependenceForIndex(Loop *I1, Loop *I2, Argument *A1, Argument *A2,
                               std::vector<ExprHandle> &indices1,
                               std::vector<ExprHandle> &indices2);

/// \returns True if the store \p W1 and the load \p R2 depend on one another
/// for the indices \p I1 and I2, that match the load/store order.
DepRelationKind depends(Loop *I1, Loop *I2, StoreStmt *W1, LoadExpr *R2);

/// \returns True if the store \p W1 and the store \p W2 depend on one another
/// for the indices \p I1 and I2, that match the store order.
DepRelationKind depends(Loop *I1, Loop *I2, StoreStmt *W1, StoreStmt *W2);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_DEPENDENCE_H
