#include "bistra/Analysis/Value.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include <array>

using namespace bistra;

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

bool bistra::computeKnownIntegerRange(Expr *e, std::pair<int, int> &range) {
  // Estimate the range of constants expressions.
  if (auto *CE = dynamic_cast<ConstantExpr *>(e)) {
    // The lower and upper bound are the constant itself.
    range.first = CE->getValue();
    range.second = CE->getValue();
    return true;
  }

  // Estimate the range for loop indices.
  if (auto *IE = dynamic_cast<IndexExpr *>(e)) {
    // Use the upper and lower bounds of the loop.
    range.first = 0;
    range.second = IE->getLoop()->getEnd();
    return true;
  }

  // Estimate the range of binary expressions.
  if (auto *BE = dynamic_cast<BinaryExpr *>(e)) {
    std::pair<int, int> L, R;
    // Compute the range of both sides:
    if (!computeKnownIntegerRange(BE->getLHS(), L) ||
        !computeKnownIntegerRange(BE->getRHS(), R))
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
