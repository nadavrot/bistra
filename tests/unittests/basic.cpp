#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
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
  auto *val = new AddExpr(ld, new ConstantFPExpr(4));
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

  p->verify();

  Program *pp = p->clone();
  delete p;
  pp->dump();
  delete pp;
}

TEST(basic, memcpy) {
  // DEST[i] = SRC[i];
  Program *p = new Program();
  auto *dest = p->addArgument("DEST", {256}, {"len"}, ElemKind::Float32Ty);
  auto *src = p->addArgument("SRC", {256}, {"len"}, ElemKind::Float32Ty);

  auto *I = new Loop("i", 256, 1);

  p->addStmt(I);

  auto *ld = new LoadExpr(src, {new IndexExpr(I)});
  auto *st = new StoreStmt(dest, {new IndexExpr(I)}, ld, false);
  I->addStmt(st);

  p->verify();
  Program *pp = p->clone();
  delete p;
  pp->dump();
  delete pp;
}

/// A visitor class that visits all nodes in the program.
struct NodeCounter : public NodeVisitor {
  unsigned stmt{0};
  unsigned expr{0};
  virtual void handle(Stmt *) { stmt++; }
  virtual void handle(Expr *) { expr++; }
};

TEST(basic, visitor_collect_indices) {
  // C[i, j] = A[i, k] * B[k, j];
  Program *p = new Program();
  p->addArgument("C", {128, 256}, {"I", "J"}, ElemKind::Float32Ty);
  p->addArgument("A", {128, 512}, {"I", "K"}, ElemKind::Float32Ty);
  p->addArgument("B", {512, 256}, {"K", "J"}, ElemKind::Float32Ty);

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
  p->verify();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 8);
  EXPECT_EQ(counter.expr, 9);
  delete p;
}

TEST(basic, time_simple_loop) {
  // C[i, j] = A[i, k] * B[k, j];
  Program *p = new Program();
  p->addArgument("C", {128, 256}, {"I", "J"}, ElemKind::Float32Ty);
  p->addArgument("A", {128, 512}, {"I", "K"}, ElemKind::Float32Ty);
  p->addArgument("B", {512, 256}, {"K", "J"}, ElemKind::Float32Ty);

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

  p->verify();

  Program *pp = p->clone();
  delete p;

  auto CB = getBackend("C");
  auto timeInSeconds = CB->evaluateCode(pp, 1000);

  EXPECT_GT(timeInSeconds, 0.1);
  delete pp;
}
