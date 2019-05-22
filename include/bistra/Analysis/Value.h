#ifndef BISTRA_ANALYSIS_VALUE_H
#define BISTRA_ANALYSIS_VALUE_H

#include <unordered_map>
#include <utility>

namespace bistra {

class Expr;
class ASTNode;
class Stmt;

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

/// Compute cost type: num memory ops, num arithmetic.
using ComputeCostTy = std::pair<uint64_t, uint64_t>;
/// Estimate the compute cost for all expressions and statements in \p S by
/// updating the map \p heatmap.
void estimateCompute(Stmt *S,
                     std::unordered_map<ASTNode *, ComputeCostTy> &heatmap);

} // end namespace bistra

#endif
