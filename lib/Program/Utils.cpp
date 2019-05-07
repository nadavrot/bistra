#include "bistra/Program/Utils.h"
#include "bistra/Program/Program.h"

#include <vector>

using namespace bistra;

namespace {
/// A visitor class that visits all IndexExpr nodes in the program. Uses
/// optional filter to collect only indices for one specific loop.
struct IndexCollector : public NodeVisitor {
  std::vector<IndexExpr *> &indices_;
  Loop *filter_;
  IndexCollector(std::vector<IndexExpr *> &indices, Loop *filter)
      : indices_(indices), filter_(filter) {}
  virtual void enter(Expr *E) override {
    if (IndexExpr *IE = dynamic_cast<IndexExpr *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && IE->getLoop() != filter_)
        return;
      indices_.push_back(IE);
    }
  }
};
} // namespace

namespace {
/// A visitor class that visits all nodes in the program.
struct LoopCollector : public NodeVisitor {
  std::vector<Loop *> &loops_;
  LoopCollector(std::vector<Loop *> &loops) : loops_(loops) {}
  virtual void enter(Stmt *E) override {
    if (Loop *L = dynamic_cast<Loop *>(E)) {
      loops_.push_back(L);
    }
  }
};
} // namespace

uint64_t HotScopeCollector::getFrequency(Scope *S) {
  for (auto &p : freqPairs_) {
    if (p.first == S)
      return p.second;
  }
  return 0;
}

std::pair<Scope *, uint64_t> HotScopeCollector::getMaxScope() {
  unsigned idx = 0;
  for (unsigned i = 1, e = freqPairs_.size(); i < e; i++) {
    if (freqPairs_[i].second > freqPairs_[idx].second) {
      idx = i;
    }
  }
  return freqPairs_[idx];
}

void HotScopeCollector::enter(Stmt *E) {
  if (auto *S = dynamic_cast<Scope *>(E)) {
    freqPairs_.push_back({S, frequency_});
  }
  // The inner part will be executed more times, based on the trip count.
  if (Loop *L = dynamic_cast<Loop *>(E)) {
    frequency_ *= L->getEnd();
  }
}

void HotScopeCollector::leave(Stmt *E) {
  // When the loop is done we divide the frequency to match the frequency of the
  // outer scope. See the implementation of 'enter'.
  if (Loop *L = dynamic_cast<Loop *>(E)) {
    frequency_ /= L->getEnd();
  }
}

void bistra::collectIndices(ASTNode *S, std::vector<IndexExpr *> &indices,
                            Loop *filter) {
  IndexCollector IC(indices, filter);
  S->visit(&IC);
}

void bistra::collectLoops(Stmt *S, std::vector<Loop *> &loops) {
  LoopCollector IC(loops);
  S->visit(&IC);
}
