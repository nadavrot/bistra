#ifndef BISTRA_ANALYSIS_VALUE_H
#define BISTRA_ANALYSIS_VALUE_H

#include "bistra/Program/UseDef.h"

#include <set>
#include <unordered_map>
#include <utility>

namespace bistra {

class Stmt;
class Expr;
class IndexExpr;
class LoadLocalExpr;
class StoreLocalStmt;
class Loop;
class IfRange;
class ASTNode;
class LocalVar;
class Argument;
class StoreStmt;
class LoadExpr;
struct ExprType;

/// Collect and return the list of statements in \p s.
std::vector<Stmt *> collectStmts(Stmt *s);

/// Collect and return the list of expressions in \p s.
std::vector<Expr *> collectExprs(Stmt *s);

/// Describes the kind of relationship some expression has when vectorizing it
/// across some dimension.
enum IndexAccessKind { Uniform, Consecutive, Other };

/// \returns the access direction and pattern for the array subscript
/// expression \p E in relation to the loop index \p L.
/// For example, in the loop (i .. 10): A[i + 3] is 'consecutive', a[j] is
/// 'uniform' and A[i * 3] is 'other'.
IndexAccessKind getIndexAccessKind(Expr *E, Loop *L);

/// \returns True if \p s is a scope.
bool isScope(Stmt *s);

/// \returns True if \p L is an innermost loop.
bool isInnermostLoop(Loop *L);

/// \returns the containing loop or nullptr.
Loop *getContainingLoop(Stmt *s);

/// Collect all of the indices in \p S into \p indices; If \p filter is set then
/// only collect indices that access the loop \p filter.
void collectIndices(ASTNode *S, std::vector<IndexExpr *> &indices,
                    Loop *filter = nullptr);

/// Collect all of the indices in \p S ; If \p filter is set then only collect
/// indices that access the loop \p filter.
std::vector<IndexExpr *> collectIndices(ASTNode *S, Loop *filter = nullptr);

/// Collect all of the load/store accesses to locals.
/// If \p filter is set then only accesses to \p filter are collected.
void collectLocals(ASTNode *S, std::vector<LoadLocalExpr *> &loads,
                   std::vector<StoreLocalStmt *> &stores,
                   LocalVar *filter = nullptr);

/// Collect all of the load/store accesses to arguments.
/// If \p filter is set then only accesses to \p filter are collected.
void collectLoadStores(ASTNode *S, std::vector<LoadExpr *> &loads,
                       std::vector<StoreStmt *> &stores,
                       Argument *filter = nullptr);

/// Collect all of the loops under statement \p S into \p loops;
void collectLoops(Stmt *S, std::vector<Loop *> &loops);

/// \returns all of the loops under statement \p S.
std::vector<Loop *> collectLoops(Stmt *S);

/// Collect all of the ifs under statement \p S into \p ifs;
void collectIfs(Stmt *S, std::vector<IfRange *> &ifs);

/// \returns a loop that has the index with the name \p name or nullptr.
Loop *getLoopByName(Stmt *S, const std::string &name);

/// \return True if the \p N depends on the loop index \p L.
/// Example: "A[i] = 4" depends on i, but not on j;
bool dependsOnLoop(ASTNode *N, Loop *L);

/// Generate the zero vector of type \p T.
Expr *getZeroExpr(ExprType &T);

/// \returns true if we can show that the loads and stores operate on different
/// buffers and don't interfer with oneanother.
bool areLoadsStoresDisjoint(const std::vector<LoadExpr *> &loads,
                            const std::vector<StoreStmt *> &stores);

/// This is similar to LLVM's simplify demanded bits.
/// Updates the possible range in \p range.
/// The nullable values in \p liveLoops are fixed to be zero. This parameter is
/// used when performing analysis for a specific context like parts of a loop.
/// \returns True if the range was computed;
bool computeKnownIntegerRange(Expr *e, std::pair<int, int> &range,
                              const std::set<Loop *> *liveLoops = nullptr);

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

/// \returns the number of elements that the load/store indiced \p indices
/// accesses for the optional live loops \p live, or zero if the load can't be
/// processed. For example, the loop for (i in 0..100) { B[i,j] = C[i,j]; }
/// accesses 100 elements, if the loop "i" is alive and the parent loop "j" is
/// fixed.
uint64_t getAccessedMemoryForSubscript(const std::vector<ExprHandle> &indices,
                                       std::set<Loop *> *live);

/// \returns True if any of the elements of \p first are in \p second.
template <typename T>
bool doSetsIntersect(const std::set<T> &first, const std::set<T> &second) {
  for (auto &e : first) {
    if (second.count(e))
      return true;
  }
  return false;
}

} // end namespace bistra

#endif
