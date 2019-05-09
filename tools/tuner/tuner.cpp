#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Transforms.h"

#include <iostream>

using namespace bistra;

Program *generateGemm(unsigned szI, unsigned szJ, unsigned szK) {
  // C[i, j] = A[i, k] * B[k, j];
  Program *p = new Program();
  auto *C = p->addArgument("C", {szI, szK}, {"I", "J"}, ElemKind::Float32Ty);
  auto *A = p->addArgument("A", {szI, szJ}, {"I", "K"}, ElemKind::Float32Ty);
  auto *B = p->addArgument("B", {szJ, szK}, {"K", "J"}, ElemKind::Float32Ty);

  auto *I = new Loop("i", szI, 1);
  auto *J = new Loop("j", szJ, 1);
  auto *K = new Loop("k", szK, 1);
  auto *zero = new StoreStmt(C, {new IndexExpr(I), new IndexExpr(J)},
                             new ConstantFPExpr(0), false);
  p->addStmt(I);
  I->addStmt(J);
  J->addStmt(zero);
  J->addStmt(K);

  auto *ldA = new LoadExpr(A, {new IndexExpr(I), new IndexExpr(K)});
  auto *ldB = new LoadExpr(B, {new IndexExpr(K), new IndexExpr(J)});
  auto *mul = new MulExpr(ldA, ldB);
  auto *st = new StoreStmt(C, {new IndexExpr(I), new IndexExpr(J)}, mul, true);
  K->addStmt(st);
  return p;
}

int main() {
  auto *p = generateGemm(512, 512, 512);
  optimizeEvaluate(p);
}
