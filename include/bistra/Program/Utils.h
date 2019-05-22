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
class IfRange;
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

/// A visitor class that visits all nodes in the program.
struct NodeCounter : public NodeVisitor {
  unsigned stmt{0};
  unsigned expr{0};
  virtual void enter(Stmt *S) override { stmt++; }
  virtual void enter(Expr *E) override { expr++; }
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

/// Prints some useful statistics about the loops in the program and their
/// execution frequency.
void dumpProgramFrequencies(Scope *P);

/// Saves the content \p content to file \p filename or aborts.
void writeFile(const std::string &filename, const std::string &content);

/// \returns the content of file \p filename or aborts.
std::string readFile(const std::string &filename);

/// Print a large number into a small number with quantiti suffix, such as K, M,
/// G, etc.
std::string prettyPrintNumber(uint64_t num);

} // namespace bistra

#endif // BISTRA_PROGRAM_UTILS_H
