#include "bistra/Program/Utils.h"
#include "bistra/Program/Program.h"

using namespace bistra;

namespace {
/// A visitor class that visits all nodes in the program.
struct IndexCollector : public NodeVisitor {
  std::vector<IndexExpr *> &indices_;
  IndexCollector(std::vector<IndexExpr *> &indices) : indices_(indices) {}
  virtual void handle(Expr *E) {
    if (IndexExpr *IE = dynamic_cast<IndexExpr *>(E)) {
      indices_.push_back(IE);
    }
  }
};
} // namespace

void bistra::collectIndices(Stmt *S, std::vector<IndexExpr *> &indices) {
  IndexCollector IC(indices);
  S->visit(&IC);
}