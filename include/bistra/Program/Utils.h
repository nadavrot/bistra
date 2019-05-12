#ifndef BISTRA_PROGRAM_UTILS_H
#define BISTRA_PROGRAM_UTILS_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bistra {

class Stmt;
class Expr;
class IndexExpr;
class LoadLocalExpr;
class StoreLocalStmt;
class Loop;
class ASTNode;
class LocalVar;
class Argument;
class StoreStmt;
class LoadExpr;
struct ExprType;

/// A visitor class that visits all nodes in the program.
class NodeVisitor {
public:
  virtual ~NodeVisitor() = default;
  // Called when we enter a statement.
  virtual void enter(Stmt *S) {}
  // Called when we enter an expression.
  virtual void enter(Expr *E) {}
  // Called when we leave a statement.
  virtual void leave(Stmt *S) {}
  // Called when we leave an expression.
  virtual void leave(Expr *E) {}
};

class Scope;

/// A visitor that collects the hot statements in the program.
struct HotScopeCollector : public NodeVisitor {
  /// Counts the number of times the statement is expected to be called.
  uint64_t frequency_{1};
  /// Maps statements to their execution frequencies.
  std::vector<std::pair<Scope *, uint64_t>> freqPairs_;

  /// \returns the frequency of the valid scope \p S;
  uint64_t getFrequency(Scope *S);

  /// \returns the hottest scope;
  std::pair<Scope *, uint64_t> getMaxScope();

  virtual void enter(Stmt *E) override;
  virtual void leave(Stmt *E) override;
};

/// Collect all of the indices in \p S into \p indices; If \p filter is set then
/// only collect indices that access the loop \p filter.
void collectIndices(ASTNode *S, std::vector<IndexExpr *> &indices,
                    Loop *filter = nullptr);

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

/// Prints some useful statistics about the loops in the program and their
/// execution frequency.
void dumpProgramFrequencies(Scope *P);

/// Saves the content \p content to file \p filename or aborts.
void writeFile(const std::string &filename, const std::string &content);

/// \returns the content of file \p filename or aborts.
std::string readFile(const std::string &filename);

} // namespace bistra

#endif // BISTRA_PROGRAM_UTILS_H
