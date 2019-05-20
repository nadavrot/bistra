#include "bistra/Transforms/Simplify.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include <algorithm>
#include <array>

using namespace bistra;

static bool isConst(Expr *e) {
  return dynamic_cast<ConstantExpr *>(e) || dynamic_cast<ConstantFPExpr *>(e);
}

static bool isOne(Expr *e) {
  if (auto *CE = dynamic_cast<ConstantExpr *>(e)) {
    return CE->getValue() == 1;
  }
  if (auto *CE = dynamic_cast<ConstantFPExpr *>(e)) {
    return CE->getValue() == 1.0;
  }
  return false;
}

static bool isZero(Expr *e) {
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

Expr *bistra::simplifyExpr(Expr *e) {

  if (BinaryExpr *BE = dynamic_cast<BinaryExpr *>(e)) {
    // Simplify operands and move constants to the RHS.
    auto *SL = simplifyExpr(BE->getLHS());
    auto *SR = simplifyExpr(BE->getRHS());
    if (!isConst(SR) && isConst(SL)) {
      std::swap(SL, SR);
    }
    BE->setLHS(SL);
    BE->setRHS(SR);

    // Simplify cases where both sides are constants.
    auto *CEL = dynamic_cast<ConstantExpr *>(SL);
    auto *CER = dynamic_cast<ConstantExpr *>(SR);
    auto *CFL = dynamic_cast<ConstantFPExpr *>(SL);
    auto *CFR = dynamic_cast<ConstantFPExpr *>(SR);

    // Both sides are integer constants.
    if (CEL && CER) {
      switch (BE->getKind()) {
      case BinaryExpr::Mul:
        return new ConstantExpr(CEL->getValue() * CER->getValue());
      case BinaryExpr::Add:
        return new ConstantExpr(CEL->getValue() + CER->getValue());
      case BinaryExpr::Div:
        return new ConstantExpr(CEL->getValue() / CER->getValue());
      case BinaryExpr::Sub:
        return new ConstantExpr(CEL->getValue() - CER->getValue());
      }
    }

    // Both sides are FP constants.
    if (CFL && CFR) {
      switch (BE->getKind()) {
      case BinaryExpr::Mul:
        return new ConstantFPExpr(CFL->getValue() * CFR->getValue());
      case BinaryExpr::Add:
        return new ConstantFPExpr(CFL->getValue() + CFR->getValue());
      case BinaryExpr::Div:
        return new ConstantFPExpr(CFL->getValue() / CFR->getValue());
      case BinaryExpr::Sub:
        return new ConstantFPExpr(CFL->getValue() - CFR->getValue());
      }
    }

    // Handle some arithmetic identities.
    switch (BE->getKind()) {
    case BinaryExpr::Mul:
      // Mul by zero.
      if (isZero(SL))
        return SL;
      if (isZero(SR))
        return SR;
      // Mul by one.
      if (isOne(SL))
        return SR;
      if (isOne(SR))
        return SL;
      break;
    case BinaryExpr::BinOpKind::Add:
      // Add zero.
      if (isZero(SL))
        return SR;
      if (isZero(SR))
        return SL;
      break;
    case BinaryExpr::Div:
      // Handle: 0 / x  and x / 1
      if (isZero(SL))
        return SL;
      if (isOne(SR))
        return SL;
      break;
    case BinaryExpr::Sub:
      break;
    }
  }

  return e;
}

namespace {
/// A visitor class that collects all loads/stores to locals.
struct ExprSimplify : public NodeVisitor {
  ExprSimplify() = default;

  bool changed_{false};

  virtual void enter(Expr *E) override {
    if (LoadExpr *LE = dynamic_cast<LoadExpr *>(E)) {
      for (auto &E : LE->getIndices()) {
        process(E);
      }
    }
  }

  /// Try to simplify the value in handle \p H and update the 'changed' flag.
  void process(ExprHandle &H) {
    auto *E = H.get();
    auto *N = ::simplifyExpr(E);
    if (N != E) {
      changed_ = true;
      H.setReference(N);
    }
  }

  virtual void enter(Stmt *S) override {
    // Simplify statements that use expressions:
    if (StoreStmt *SS = dynamic_cast<StoreStmt *>(S)) {
      for (auto &E : SS->getIndices()) {
        process(E);
      }
      process(SS->getValue());
    }
    if (StoreLocalStmt *SLS = dynamic_cast<StoreLocalStmt *>(S)) {
      return process(SLS->getValue());
    }
    if (IfRange *IR = dynamic_cast<IfRange *>(S)) {
      return process(IR->getIndex());
    }
  }
};
} // namespace

enum RangeRelation { Intersect, Disjoint, Subset };
/// \returns the relationship between sets A and B.
static RangeRelation getRangeRelation(std::pair<int, int> A,
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

/// Removes empty loops.
/// \returns True if some code was modified.
static bool removeEmptyLoops(Stmt *s) {
  std::vector<Loop *> loops;
  collectLoops(s, loops);

  bool changed = false;
  // Remove empty loops:
  for (auto *L : loops) {
    if (L->isEmpty()) {
      ((Scope *)L->getParent())->removeStmt(L);
      changed = true;
    }
  }
  return changed;
}

/// Removes loops that execute once.
/// \returns True if some code was modified.
static bool removeTrip1Loops(Stmt *s) {
  std::vector<Loop *> loops;
  collectLoops(s, loops);
  bool changed = false;

  // Remove loops of tripcount-1:
  for (auto *L : loops) {
    // If we need to eliminate loops that perform just one iteration.
    if (L->getEnd() != L->getStride())
      continue;

    std::vector<IndexExpr *> indices;
    collectIndices(L, indices, L);

    // Replace all of the loop indices with zero.
    for (auto *idx : indices) {
      idx->replaceUseWith(new ConstantExpr(0));
    }

    // Move the loop body to the parent scope.
    Scope *parent = (Scope *)L->getParent();
    for (auto &E : L->getBody()) {
      parent->insertBeforeStmt(E.get(), L);
    }

    // Delete the loop body with the dangling null instructions that moved.
    parent->removeStmt(L);
    changed = true;
  }
  return changed;
}

/// Simplify all of the Ifs.
/// \returns True if some code was modified.
static bool simplifyIfs(Stmt *s) {
  bool changed = false;
  std::vector<IfRange *> ifs;
  collectIfs(s, ifs);

  // Eliminate ifs that are statically known not to be executed.
  for (auto *ifs : ifs) {
    // The index range.
    std::pair<int, int> ir;
    // Try to assess the if index range.
    if (!computeKnownIntegerRange(ifs->getIndex(), ir))
      continue;

    // Compute the relationship between the index and if ranges.
    auto rel = getRangeRelation(ir, ifs->getRange());

    switch (rel) {
    case Intersect: {
      // No useful information. Do nothing.
      continue;
    }

    case RangeRelation::Disjoint: {
      // Remove the IF if the ranges don't intersect.
      ((Scope *)ifs->getParent())->removeStmt(ifs);
      changed = true;
      continue;
    }
    case Subset:
      // The index always falls within the if range. Remove the if and keep
      // the if content.
      CloneCtx map;
      Scope *parent = ((Scope *)ifs->getParent());
      for (auto &S : ifs->getBody()) {
        // Clone the body of the IF right before the IF.
        parent->insertBeforeStmt(S->clone(map), ifs);
      }

      // Remove the IF.
      parent->removeStmt(ifs);
      changed = true;
      continue;
      ;
    }
  }

  return changed;
}

bool bistra::simplify(Stmt *s) {
  std::vector<Loop *> loops;
  collectLoops(s, loops);

  bool changed = false;

  // Simplify all of the expressions in the graph.
  ExprSimplify ES;
  s->visit(&ES);
  changed |= ES.changed_;

  changed |= removeEmptyLoops(s);

  changed |= simplifyIfs(s);

  changed |= removeTrip1Loops(s);

  return changed;
}
