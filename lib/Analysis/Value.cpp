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
