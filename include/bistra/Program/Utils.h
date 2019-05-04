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

struct Stmt;
struct Expr;
struct IndexExpr;

/// A visitor class that visits all nodes in the program.
struct NodeVisitor {
  virtual ~NodeVisitor() = default;
  // Called when we find a statement.
  virtual void handle(Stmt *S){};
  // Called when we find an expression.
  virtual void handle(Expr *E){};
};

/// Collect all of the indices under statement \p S into \p indices;
void collectIndices(Stmt *S, std::vector<IndexExpr *> &indices);

} // namespace bistra

#endif // BISTRA_PROGRAM_UTILS_H
