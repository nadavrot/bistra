#include "bistra/Transforms/Dependence.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Program/Pragma.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"

#include <set>

using namespace bistra;

/// \returns True if the expression \p e is the IndexExpr for loop \p L.
/// if \p recursive is set then search in sub-expressions of \p e.
static bool isRefOfLoop(Expr *e, Loop *L, bool recursive) {
  if (auto *IE = dynamic_cast<IndexExpr *>(e)) {
    return IE->getLoop() == L;
  }
  if (recursive)
    return collectIndices(e, L).size();

  return false;
}

DepRelationKind bistra::depends(Loop *I1, Loop *I2, StoreStmt *W1,
                                LoadExpr *R2) {
  return checkWeakSIVDependenceForIndex(I1, I2, W1->getDest(), R2->getDest(),
                                        W1->getIndices(), R2->getIndices());
}

DepRelationKind bistra::depends(Loop *I1, Loop *I2, StoreStmt *W1,
                                StoreStmt *W2) {
  return checkWeakSIVDependenceForIndex(I1, I2, W1->getDest(), W2->getDest(),
                                        W1->getIndices(), W2->getIndices());
}

bistra::DepRelationKind bistra::checkWeakSIVDependenceForIndex(
    Loop *I1, Loop *I2, Argument *A1, Argument *A2,
    std::vector<ExprHandle> &indices1, std::vector<ExprHandle> &indices2) {
  // Accessing a different buffer. No dep.
  if (A1 != A2)
    return DepRelationKind::NoDep;

  assert(indices1.size() == indices2.size() && "Invalid index vector");

  for (int i = 0; i < indices1.size(); i++) {
    // Check direct access to I and J.
    bool isIndex1 = isRefOfLoop(indices1[i], I1, false);
    bool isIndex2 = isRefOfLoop(indices2[i], I2, false);
    if (isIndex1 && isIndex2) {
      // Immediate access at the same array index are allowed.
      continue;
    }

    // Check if the possible ranges of indices are disjoint.
    // For example: A[0..10] A[20..30]
    std::pair<int, int> range1;
    std::pair<int, int> range2;
    bool r1 = computeKnownIntegerRange(indices1[i], range1);
    bool r2 = computeKnownIntegerRange(indices2[i], range2);
    if (r1 && r2) {
      // If the analysis worked: check if the ranges are disjoint.
      if (range1.second < range2.first || range2.second < range1.first)
        continue;
    }

    // Any other index access is disallowed. The buffers depend on each other.
    // Example: A[I] vs. B[I+1]; A[I, 0] vs. B[0, I]; A[I] vs. B[0];
    bool hasIndex1 = isRefOfLoop(indices1[i], I1, true);
    bool hasIndex2 = isRefOfLoop(indices2[i], I2, true);
    if (hasIndex1 || hasIndex2)
      return DepRelationKind::SomeDep;
  }

  // The subscript dependency always overlaps for this index.
  return DepRelationKind::Equals;
}
