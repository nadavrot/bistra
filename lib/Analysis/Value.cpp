#include "bistra/Analysis/Value.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include <array>
#include <set>

using namespace bistra;

IndexAccessKind bistra::getIndexAccessKind(Expr *E, Loop *L) {
  if (IndexExpr *IE = dynamic_cast<IndexExpr *>(E)) {
    if (IE->getLoop() == L) {
      return IndexAccessKind::Consecutive;
    }
    return IndexAccessKind::Uniform;
  }

  if (BinaryExpr *BE = dynamic_cast<BinaryExpr *>(E)) {
    auto LK = getIndexAccessKind(BE->getLHS(), L);
    auto RK = getIndexAccessKind(BE->getRHS(), L);

    switch (BE->getKind()) {
      // Mul expressions. Example:  [d * J];
    case BinaryExpr::BinOpKind::Mul: {
      if (LK == IndexAccessKind::Uniform && RK == IndexAccessKind::Uniform)
        return Uniform;
    }

      // Addition expressions. Example:  [4 + J];
    case BinaryExpr::BinOpKind::Add: {
      if (LK == IndexAccessKind::Other || RK == IndexAccessKind::Other)
        return IndexAccessKind::Other;
      if (LK == IndexAccessKind::Uniform && RK == IndexAccessKind::Uniform)
        return Uniform;
      return Consecutive;
    }

    default:
      if (LK == IndexAccessKind::Uniform && RK == IndexAccessKind::Uniform)
        return Uniform;
      return Other;
    }
  }

  if (dynamic_cast<ConstantExpr *>(E) || dynamic_cast<ConstantFPExpr *>(E)) {
    return IndexAccessKind::Uniform;
  }

  return IndexAccessKind::Other;
}

bool bistra::isScope(Stmt *s) { return dynamic_cast<Scope *>(s); }

bool bistra::isInnermostLoop(Loop *L) {
  for (auto &S : L->getBody()) {
    if (dynamic_cast<Scope *>(S.get()))
      return false;
  }
  return true;
}

Loop *bistra::getContainingLoop(Stmt *s) {
  ASTNode *p = s;
  while (p) {
    p = p->getParent();
    if (Loop *L = dynamic_cast<Loop *>(p))
      return L;
  }
  return nullptr;
}

namespace {
/// A visitor class that collects all loads/stores to locals.
struct LocalsCollector : public NodeVisitor {
  std::vector<LoadLocalExpr *> &loads_;
  std::vector<StoreLocalStmt *> &stores_;
  LocalVar *filter_;
  LocalsCollector(std::vector<LoadLocalExpr *> &loads,
                  std::vector<StoreLocalStmt *> &stores, LocalVar *filter)
      : loads_(loads), stores_(stores), filter_(filter) {}

  virtual void enter(Expr *E) override {
    if (auto *LL = dynamic_cast<LoadLocalExpr *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && LL->getDest() != filter_)
        return;
      loads_.push_back(LL);
    }
  }

  virtual void enter(Stmt *E) override {
    if (auto *SL = dynamic_cast<StoreLocalStmt *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && SL->getDest() != filter_)
        return;
      stores_.push_back(SL);
    }
  }
};
} // namespace

namespace {
/// A visitor class that collects all loads/stores.
struct LoadStoreCollector : public NodeVisitor {
  std::vector<LoadExpr *> &loads_;
  std::vector<StoreStmt *> &stores_;
  Argument *filter_;

  LoadStoreCollector(std::vector<LoadExpr *> &loads,
                     std::vector<StoreStmt *> &stores, Argument *filter)
      : loads_(loads), stores_(stores), filter_(filter) {}

  virtual void enter(Expr *E) override {
    if (auto *LL = dynamic_cast<LoadExpr *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && LL->getDest() != filter_)
        return;
      loads_.push_back(LL);
    }
  }

  virtual void enter(Stmt *E) override {
    if (auto *SL = dynamic_cast<StoreStmt *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && SL->getDest() != filter_)
        return;
      stores_.push_back(SL);
    }
  }
};
} // namespace

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
/// A visitor class that visits all loops in the program.
struct LoopCollector : public NodeVisitor {
  std::vector<Loop *> &loops_;
  LoopCollector(std::vector<Loop *> &loops) : loops_(loops) {}
  virtual void enter(Stmt *E) override {
    if (Loop *L = dynamic_cast<Loop *>(E)) {
      loops_.push_back(L);
    }
  }
};

/// A visitor class that visits all ifs in the program.
struct IfCollector : public NodeVisitor {
  std::vector<IfRange *> &ifs_;
  IfCollector(std::vector<IfRange *> &ifs) : ifs_(ifs) {}
  virtual void enter(Stmt *E) override {
    if (auto *I = dynamic_cast<IfRange *>(E)) {
      ifs_.push_back(I);
    }
  }
};
} // namespace

void bistra::collectLocals(ASTNode *S, std::vector<LoadLocalExpr *> &loads,
                           std::vector<StoreLocalStmt *> &stores,
                           LocalVar *filter) {
  LocalsCollector IC(loads, stores, filter);
  S->visit(&IC);
}

/// Collect all of the load/store accesses to arguments.
/// If \p filter is set then only accesses to \p filter are collected.
void bistra::collectLoadStores(ASTNode *S, std::vector<LoadExpr *> &loads,
                               std::vector<StoreStmt *> &stores,
                               Argument *filter) {
  LoadStoreCollector IC(loads, stores, filter);
  S->visit(&IC);
}

void bistra::collectIndices(ASTNode *S, std::vector<IndexExpr *> &indices,
                            Loop *filter) {
  IndexCollector IC(indices, filter);
  S->visit(&IC);
}

bool bistra::dependsOnLoop(ASTNode *N, Loop *L) {
  std::vector<IndexExpr *> indices;
  collectIndices(N, indices, L);
  return indices.size();
}

void bistra::collectLoops(Stmt *S, std::vector<Loop *> &loops) {
  LoopCollector IC(loops);
  S->visit(&IC);
}

std::vector<Loop *> bistra::collectLoops(Stmt *S) {
  std::vector<Loop *> loops;
  collectLoops(S, loops);
  return loops;
}

void bistra::collectIfs(Stmt *S, std::vector<IfRange *> &ifs) {
  IfCollector IC(ifs);
  S->visit(&IC);
}

Loop *bistra::getLoopByName(Stmt *S, const std::string &name) {
  std::vector<Loop *> loops;
  collectLoops(S, loops);
  for (auto *L : loops) {
    if (L->getName() == name) {
      return L;
    }
  }
  return nullptr;
}

/// Collect all of the load/store accesses to locals.
/// If \p filter is set then only accesses to \p filter are collected.
void bistra::collectLocals(ASTNode *S, std::vector<LoadLocalExpr *> &loads,
                           std::vector<StoreLocalStmt *> &stores,
                           LocalVar *filter);

Expr *bistra::getZeroExpr(ExprType &T) {
  Expr *ret;
  // Zero scalar:
  if (T.isIndexTy()) {
    ret = new ConstantExpr(0);
  } else {
    ret = new ConstantFPExpr(0.0);
  }

  // Widen if we are requested a vector.
  if (T.isVector()) {
    ret = new BroadcastExpr(ret, T.getWidth());
  }

  assert(ret->getType() == T);
  return ret;
}

bool bistra::areLoadsStoresDisjoint(const std::vector<LoadExpr *> &loads,
                                    const std::vector<StoreStmt *> &stores) {
  std::set<Argument *> writes;
  // Collect the write destination.
  for (auto *st : stores) {
    writes.insert(st->getDest());
  }
  for (auto *ld : loads) {
    // A pair of load/store wrote into the same buffer. Aborting.
    if (writes.count(ld->getDest())) {
      return false;
    }
  }
  return true;
}

bool bistra::isConst(Expr *e) {
  return dynamic_cast<ConstantExpr *>(e) || dynamic_cast<ConstantFPExpr *>(e);
}

bool bistra::isOne(Expr *e) {
  if (auto *CE = dynamic_cast<ConstantExpr *>(e)) {
    return CE->getValue() == 1;
  }
  if (auto *CE = dynamic_cast<ConstantFPExpr *>(e)) {
    return CE->getValue() == 1.0;
  }
  return false;
}

bool bistra::isZero(Expr *e) {
  if (auto *CE = dynamic_cast<ConstantExpr *>(e)) {
    return CE->getValue() == 0;
  }
  if (auto *CE = dynamic_cast<ConstantFPExpr *>(e)) {
    return CE->getValue() == 0.0;
  }
  return false;
}

bool bistra::computeKnownIntegerRange(Expr *e, std::pair<int, int> &range,
                                      const std::set<Loop *> *liveLoops) {
  // Estimate the range of constants expressions.
  if (auto *CE = dynamic_cast<ConstantExpr *>(e)) {
    // The lower and upper bound are the constant itself.
    range.first = CE->getValue();
    range.second = CE->getValue();
    return true;
  }

  // Estimate the range for loop indices.
  if (auto *IE = dynamic_cast<IndexExpr *>(e)) {
    // This is a frozen loop. Assume that the range is fixed on 0.
    if (liveLoops && !liveLoops->count(IE->getLoop())) {
      range.first = 0;
      range.second = 0;
      return true;
    }

    // Use the upper and lower bounds of the loop.
    range.first = 0;
    // The loop jumps in steps (stride). The last value of the loop is the end
    // range minus the stride value. The stride must evenly divide the loop.
    range.second = IE->getLoop()->getEnd() - IE->getLoop()->getStride();
    return true;
  }

  // Estimate the range of binary expressions.
  if (auto *BE = dynamic_cast<BinaryExpr *>(e)) {
    std::pair<int, int> L, R;
    // Compute the range of both sides:
    if (!computeKnownIntegerRange(BE->getLHS(), L, liveLoops) ||
        !computeKnownIntegerRange(BE->getRHS(), R, liveLoops))
      return false;

    switch (BE->getKind()) {
    case BinaryExpr::Mul: {
      // Try all combinations.
      std::array<int, 4> perm{{L.first * R.first, L.first * R.second,
                               L.second * R.first, L.second * R.second}};
      range.first = *std::min_element(perm.begin(), perm.end());
      range.second = *std::max_element(perm.begin(), perm.end());
      return true;
    }
    case BinaryExpr::Add: {
      // Try all combinations.
      std::array<int, 4> perm{{L.first + R.first, L.first + R.second,
                               L.second + R.first, L.second + R.second}};
      range.first = *std::min_element(perm.begin(), perm.end());
      range.second = *std::max_element(perm.begin(), perm.end());
      return true;
    }
    case BinaryExpr::Div:
      return false;
    case BinaryExpr::Sub: {
      // Try all combinations.
      std::array<int, 4> perm{{L.first - R.first, L.first - R.second,
                               L.second - R.first, L.second - R.second}};
      range.first = *std::min_element(perm.begin(), perm.end());
      range.second = *std::max_element(perm.begin(), perm.end());
      return true;
    }

    case BinaryExpr::Min: {
      range.first = std::min(L.first, R.first);
      range.second = std::min(L.second, R.second);
      return true;
    }

    case BinaryExpr::Max: {
      range.first = std::min(L.first, R.first);
      range.second = std::min(L.second, R.second);
      return true;
    }

    case BinaryExpr::Pow:
      return false;
    }
  }

  return false;
}

RangeRelation bistra::getRangeRelation(std::pair<int, int> A,
                                       std::pair<int, int> B) {
  // A is contained inside B.
  if (A.first >= B.first && A.second <= B.second) {
    return RangeRelation::Subset;
  }

  // A and B are disjoint.
  if (A.second <= B.first || A.second <= B.first) {
    return RangeRelation::Disjoint;
  }

  // A and B overlap.
  return RangeRelation::Intersect;
}

namespace {
/// Calculates the roofline model for the program.
struct ComputeEstimator : public NodeVisitor {
  std::unordered_map<ASTNode *, ComputeCostTy> &heatmap_;

  ComputeEstimator(std::unordered_map<ASTNode *, ComputeCostTy> &heatmap)
      : heatmap_(heatmap) {}

  virtual void leave(Expr *E) override {
    // Loads count as one memory op and zero compute.
    if (auto *LE = dynamic_cast<LoadExpr *>(E)) {
      int width = LE->getType().getWidth();
      heatmap_[LE] = {width, 0};
      return;
    }
    // Load locals count as zeo memory op and zero compute.
    if (auto *LL = dynamic_cast<LoadLocalExpr *>(E)) {
      heatmap_[LL] = {0, 0};
      return;
    }
    // Binary ops add one arithmetic cost to the cost of both sides.
    if (auto *BE = dynamic_cast<BinaryExpr *>(E)) {
      assert(heatmap_.count(BE->getLHS()));
      assert(heatmap_.count(BE->getRHS()));
      auto LHS = heatmap_[BE->getLHS()];
      auto RHS = heatmap_[BE->getRHS()];
      int width = BE->getType().getWidth();
      // Don't count index arithmetic as arithmetic.
      int cost = BE->getLHS()->getType().isIndexTy() ? 0 : width;
      heatmap_[BE] = {LHS.first + RHS.first, cost + LHS.second + RHS.second};
      return;
    }

    // Unary arithmetic ops add one arithmetic unit.
    if (auto *UE = dynamic_cast<UnaryExpr *>(E)) {
      assert(heatmap_.count(UE->getVal()));
      auto VV = heatmap_[UE->getVal()];
      int width = UE->getType().getWidth();
      VV.second += width;
      heatmap_[UE] = VV;
      return;
    }

    // Broadcast counts as one arithmetic op.
    if (auto *BE = dynamic_cast<BroadcastExpr *>(E)) {
      assert(heatmap_.count(BE->getValue()));
      auto V = heatmap_[BE->getValue()];
      heatmap_[BE] = {V.first, 1 + V.second};
      return;
    }
    // Constants and indices have no cost.
    if (dynamic_cast<IndexExpr *>(E) || dynamic_cast<ConstantExpr *>(E) ||
        dynamic_cast<ConstantFPExpr *>(E)) {
      heatmap_[E] = {0, 0};
      return;
    }
    assert(false && "Unknown expression");
  }

  virtual void leave(Stmt *E) override {
    // Loop expressions multipliy the cost of the sum of the body cost.
    if (auto *LE = dynamic_cast<Loop *>(E)) {
      ComputeCostTy total = {0, 0};
      auto tripcount = LE->getEnd() / LE->getStride();
      // Add the cost of all sub-expressions.
      for (auto &s : LE->getBody()) {
        assert(heatmap_.count(s.get()));
        auto res = heatmap_[s.get()];
        total.first += res.first * tripcount;
        total.second += res.second * tripcount;
      }
      heatmap_[LE] = total;
      return;
    }

    // If expressions accumulate the cost of sub-stmt and add the cost of the
    // if-check. We assume 100% success rate.
    if (auto *IR = dynamic_cast<IfRange *>(E)) {
      auto idx = IR->getIndex().get();
      assert(heatmap_.count(idx));
      ComputeCostTy total = heatmap_[idx];

      // Add the cost of all sub-expressions.
      for (auto &s : IR->getBody()) {
        assert(heatmap_.count(s.get()));
        auto res = heatmap_[s.get()];
        total.first += res.first;
        total.second += res.second;
      }
      heatmap_[IR] = total;
      return;
    }

    // Programs accumulate the cost of sub-stmts.
    if (auto *P = dynamic_cast<Program *>(E)) {
      ComputeCostTy total = {0, 0};
      // Add the cost of all sub-expressions.
      for (auto &s : P->getBody()) {
        assert(heatmap_.count(s.get()));
        auto res = heatmap_[s.get()];
        total.first += res.first;
        total.second += res.second;
      }
      heatmap_[P] = total;
      return;
    }

    // Stores are considered as one memory op, plus the cost of the value.
    if (auto *SS = dynamic_cast<StoreStmt *>(E)) {
      auto val = SS->getValue().get();
      int width = val->getType().getWidth();
      assert(heatmap_.count(val));
      ComputeCostTy total = heatmap_[val];
      if (SS->isAccumulate()) {
        // Accumulate is load+add+store.
        total.first += 2 * width;
        total.second += 1 * width;
      } else {
        total.first += 1 * width;
      }
      heatmap_[SS] = total;
      return;
    }

    // Stores to locals are considered as zero memory ops.
    if (auto *SL = dynamic_cast<StoreLocalStmt *>(E)) {
      auto val = SL->getValue().get();
      int width = val->getType().getWidth();
      assert(heatmap_.count(val));
      ComputeCostTy total = heatmap_[val];
      if (SL->isAccumulate()) {
        total.second += width;
      }
      heatmap_[SL] = total;
      return;
    }

    assert(false && "Unknown statement");
  }
};
} // namespace

void bistra::estimateCompute(
    Stmt *S, std::unordered_map<ASTNode *, ComputeCostTy> &heatmap) {
  ComputeEstimator CE(heatmap);
  S->visit(&CE);
}

uint64_t
bistra::getAccessedMemoryForSubscript(const std::vector<ExprHandle> &indices,
                                      std::set<Loop *> *live) {
  int span = 1;
  // Multipliy the accessed range of all indices. The range that the load can
  // access is defined as the multiplication of all of the indices, where
  // non-live loops are fixed to zero. For example, for the live loops 'i' and
  // 'j' and the fixed loop k: A[i, j, k] -> range(i) * range(j) * 1
  for (auto &idx : indices) {
    std::pair<int, int> range;
    if (!computeKnownIntegerRange(idx.get(), range, live))
      return 0;
    span *= range.second - range.first + 1;
  }

  return span;
}
