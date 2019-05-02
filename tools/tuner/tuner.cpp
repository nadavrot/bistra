#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Program/Program.h"

#include <iostream>

using namespace bistra;

int main() {
  const int sizeM = 128;
  const int sizeN = 256;
  const int sizeV = 1024;

  // C[i, j] = A[i, k] * B[k, j];
  Program *p = new Program();
  p->addArgument("C", {sizeM, sizeN}, {"I", "J"}, ElemKind::Float32Ty);
  p->addArgument("A", {sizeM, sizeV}, {"I", "K"}, ElemKind::Float32Ty);
  p->addArgument("B", {sizeV, sizeN}, {"K", "J"}, ElemKind::Float32Ty);

  auto *C = p->getArg(0);
  auto *A = p->getArg(1);
  auto *B = p->getArg(2);

  auto *I = new Loop("i", sizeM, 1);
  auto *J = new Loop("j", sizeN, 1);
  auto *K = new Loop("k", sizeV, 1);
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

  p->verify();
  p->dump();

  Backend *CB = getBackend("C");
  auto cpp = CB->emitCode(*p);
  std::cout << cpp;
}
