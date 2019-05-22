#ifndef BISTRA_ANALYSIS_VISITORS_H
#define BISTRA_ANALYSIS_VISITORS_H

namespace bistra {

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

} // end namespace bistra

#endif
