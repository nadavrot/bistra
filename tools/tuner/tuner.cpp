#include "bistra/CodeGen/CodeGen.h"
#include "bistra/Program/Program.h"

#include <iostream>

using namespace bistra;

int main() {
  // C[i, j] = A[i, k] * B[k, j];
  Program *p = new Program();
  p->addArgument("C", {128, 32}, {"I", "J"}, ElemKind::Float32Ty);
  p->addArgument("A", {128, 64}, {"I", "K"}, ElemKind::Float32Ty);
  p->addArgument("B", {64, 32}, {"K", "J"}, ElemKind::Float32Ty);

  auto *C = p->getArg(0);
  auto *B = p->getArg(1);
  auto *A = p->getArg(2);

  auto *I = new Loop("i", 128, 1);
  auto *J = new Loop("j", 32, 1);
  auto *K = new Loop("k", 64, 1);

  p->addStmt(I);
  I->addStmt(J);
  J->addStmt(K);

  auto *ldB = new LoadExpr(B, {new IndexExpr(I), new IndexExpr(K)});
  auto *ldA = new LoadExpr(A, {new IndexExpr(K), new IndexExpr(J)});
  auto *mul = new MulExpr(ldA, ldB);
  auto *st = new StoreStmt(C, {new IndexExpr(I), new IndexExpr(J)}, mul, true);
  K->addStmt(st);

  p->dump();
}
