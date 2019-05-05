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
class Loop;

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

/// Collect all of the indices under statement \p S into \p indices;
void collectIndices(Stmt *S, std::vector<IndexExpr *> &indices);

/// Collect all of the loops under statement \p S into \p loops;
void collectLoops(Stmt *S, std::vector<Loop *> &loops);

} // namespace bistra

#endif // BISTRA_PROGRAM_UTILS_H
