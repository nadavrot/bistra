#include "bistra/Transforms/Simplify.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include <algorithm>
#include <array>

using namespace bistra;

Expr *bistra::simplifyExpr(Expr *e) {

  if (BinaryExpr *BE = dynamic_cast<BinaryExpr *>(e)) {
    // Simplify operands and move constants to the RHS.
    auto *SL = simplifyExpr(BE->getLHS());
    auto *SR = simplifyExpr(BE->getRHS());
    if (!isConst(SR) && isConst(SL) && BE->isCommutative()) {
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
      case BinaryExpr::Min:
        return new ConstantExpr(std::min(CEL->getValue(), CER->getValue()));
      case BinaryExpr::Max:
        return new ConstantExpr(std::max(CEL->getValue(), CER->getValue()));
      case BinaryExpr::Pow:
        return new ConstantExpr(std::pow(CEL->getValue(), CER->getValue()));
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
      case BinaryExpr::Min:
        return new ConstantFPExpr(std::min(CEL->getValue(), CER->getValue()));
      case BinaryExpr::Max:
        return new ConstantFPExpr(std::max(CEL->getValue(), CER->getValue()));
      case BinaryExpr::Pow:
        return new ConstantFPExpr(std::pow(CEL->getValue(), CER->getValue()));
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
    case BinaryExpr::Min:
    case BinaryExpr::Max:
    case BinaryExpr::Pow:
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
      Scope *parent = ((Scope *)ifs->getParent());
      for (auto &S : ifs->getBody()) {
        // Clone the body of the IF right before the IF.
        parent->insertBeforeStmt(S.take(), ifs);
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
