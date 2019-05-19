#include "bistra/Transforms/Simplify.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

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
