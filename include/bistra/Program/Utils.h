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
  // Called when we find a statement.
  virtual void handle(Stmt *S){};
  // Called when we find an expression.
  virtual void handle(Expr *E){};
};

/// Collect all of the indices under statement \p S into \p indices;
void collectIndices(Stmt *S, std::vector<IndexExpr *> &indices);

/// Collect all of the loops under statement \p S into \p loops;
void collectLoops(Stmt *S, std::vector<Loop *> &loops);

} // namespace bistra

#endif // BISTRA_PROGRAM_UTILS_H
