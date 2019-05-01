#include "bistra/Program/Program.h"
#include "gtest/gtest.h"

using namespace bistra;

// Check that we can build a simple program, clone a graph and dump it.
TEST(basic, builder) {
  Program *p = new Program();
  p->addArgument("bar", {32, 32}, {"X", "Y"}, ElemKind::Float32Ty);
  p->addArgument("foo", {10, 32, 32, 4}, {"N", "H", "W", "C"},
                 ElemKind::Float32Ty);
  auto *L = new Loop("i", 10, 1);
  auto *K = new Loop("j", 10, 1);
  L->addStmt(K);
  p->addStmt(L);

  auto *buff = p->getArg(0);
  auto *ld = new LoadExpr(buff, {new IndexExpr(K), new IndexExpr(L)});
  auto *val = new AddExpr(ld, new ConstantExpr(4));
  auto *store =
      new StoreStmt(buff, {new IndexExpr(K), new IndexExpr(L)}, val, true);
  K->addStmt(store);
  Program *pp = p->clone();
  delete p;
  pp->dump();
  delete pp;
}

TEST(basic, matmul) {
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

  Program *pp = p->clone();
  delete p;
  pp->dump();
  delete pp;
}
