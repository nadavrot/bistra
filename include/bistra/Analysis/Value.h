#ifndef BISTRA_ANALYSIS_VALUE_H
#define BISTRA_ANALYSIS_VALUE_H

#include <utility>

namespace bistra {

class Expr;

/// This is similar to LLVM's simplify demanded bits.
/// Updates the possible range in \p range.
/// \returns True if the range was computed;
bool computeKnownIntegerRange(Expr *e, std::pair<int, int> &range);

/// \returns True if \e is a constant (int or fp).
bool isConst(Expr *e);

/// \returns True if \e is a '1' constant (int or fp).
bool isOne(Expr *e);

/// \returns True if \e is a zero constant (int or fp).
bool isZero(Expr *e);

enum RangeRelation { Intersect, Disjoint, Subset };
/// \returns the relationship between sets A and B.
RangeRelation getRangeRelation(std::pair<int, int> A, std::pair<int, int> B);

} // end namespace bistra

#endif
