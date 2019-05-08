#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Transforms.h"

#include "gtest/gtest.h"

using namespace bistra;

// Check that we can build a simple program, clone a graph and dump it.
TEST(basic, builder) {
  Program *p = new Program();
  p->addArgument("bar", {32, 32}, {"X", "Y"}, ElemKind::Float32Ty);
  p->addArgument("input", {32, 32}, {"X", "Y"}, ElemKind::Float32Ty);
  p->addArgument("foo", {10, 32, 32, 4}, {"N", "H", "W", "C"},
                 ElemKind::Float32Ty);
  auto *L = new Loop("i", 10, 1);
  auto *K = new Loop("j", 10, 1);
  L->addStmt(K);
  p->addStmt(L);

  auto *A = p->getArg(0);
  auto *B = p->getArg(1);

  auto *ld = new LoadExpr(A, {new IndexExpr(K), new IndexExpr(L)});
  auto *val = new AddExpr(ld, new ConstantFPExpr(4));
  auto *store =
      new StoreStmt(B, {new IndexExpr(K), new IndexExpr(L)}, val, true);
  K->addStmt(store);
  p->dump();

  // Check the ownership of the nodes in the graph.
  EXPECT_EQ(L->getParent(), p);
  EXPECT_EQ(K->getParent(), L);
  EXPECT_EQ(store->getParent(), K);
  EXPECT_EQ(ld->getParent(), val);
  EXPECT_EQ(val->getParent(), store);

  HotScopeCollector HCS;
  p->visit(&HCS);
  EXPECT_EQ(HCS.getFrequency(p), 1);  // The main is executed once.
  EXPECT_EQ(HCS.getFrequency(L), 1);  // The loop is still executed once.
  EXPECT_EQ(HCS.getFrequency(K), 10); // The inner loop is executed 10 times.
  EXPECT_EQ(HCS.getMaxScope().second, 10);
  EXPECT_EQ(HCS.getMaxScope().first, K);

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
  virtual void enter(Stmt *S) override { stmt++; }
  virtual void enter(Expr *E) override { expr++; }
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

  // Check that the ownership of the node is correct.
  EXPECT_EQ(mul->getLHS()->getParent(), mul);
  EXPECT_EQ(mul->getRHS()->getParent(), mul);
  EXPECT_EQ(mul->getParent(), st);
  EXPECT_EQ(ldB->getParent(), mul);

  K->addStmt(st);
  p->verify();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 5);
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
  CB->evaluateCode(pp, 10);
  delete pp;
}

TEST(basic, tile_loop) {
  Program *p = new Program();
  p->addArgument("A", {125}, {"X"}, ElemKind::Float32Ty);
  p->addArgument("B", {125}, {"X"}, ElemKind::Float32Ty);

  auto *B = p->getArg(0);
  auto *A = p->getArg(1);

  auto *I = new Loop("i", 125, 1);

  p->addStmt(I);

  auto *ldB = new LoadExpr(B, {new IndexExpr(I)});
  auto *cf = new ConstantFPExpr(1.5);
  auto *mul = new MulExpr(ldB, cf);
  auto *st = new StoreStmt(A, {new IndexExpr(I)}, mul, true);
  I->addStmt(st);

  p->verify();

  ::tile(I, 5);

  p->dump();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 4);
  EXPECT_EQ(counter.expr, 13);
  delete p->clone();
  delete p;
}

TEST(basic, unroll_loop) {
  Program *p = new Program();
  p->addArgument("A", {10}, {"X"}, ElemKind::Float32Ty);

  auto *A = p->getArg(0);

  auto *I = new Loop("i", 10, 1);
  p->addStmt(I);
  auto *st1 =
      new StoreStmt(A, {new IndexExpr(I)}, new ConstantFPExpr(0.1), false);
  I->addStmt(st1);

  p->verify();
  p->dump();
  ::unrollLoop(I, 20);
  p->dump();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 11);
  EXPECT_EQ(counter.expr, 20);
  delete p->clone();
  delete p;
}

TEST(basic, peel_loop) {
  // DEST[i] = SRC[i];
  Program *p = new Program();
  auto *dest = p->addArgument("DEST", {260}, {"len"}, ElemKind::Float32Ty);
  auto *src = p->addArgument("SRC", {260}, {"len"}, ElemKind::Float32Ty);

  auto *I = new Loop("i", 260, 1);

  p->addStmt(I);

  auto *ld = new LoadExpr(src, {new IndexExpr(I)});
  auto *st = new StoreStmt(dest, {new IndexExpr(I)}, ld, false);
  I->addStmt(st);

  p->verify();
  p->dump();
  ::peelLoop(I, 256);
  p->dump();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 5);
  EXPECT_EQ(counter.expr, 10);
  delete p->clone();
  delete p;
}

TEST(basic, peel_loop2) {
  Program *p = new Program();
  p->addArgument("A", {60, 60}, {"X", "Y"}, ElemKind::Float32Ty);
  p->addArgument("B", {60, 60}, {"X", "Y"}, ElemKind::Float32Ty);
  p->addArgument("C", {60, 60}, {"X", "Y"}, ElemKind::Float32Ty);

  auto *A = p->getArg(0);
  auto *B = p->getArg(1);
  auto *C = p->getArg(2);

  auto *I = new Loop("i", 60, 1);
  auto *J1 = new Loop("j1", 60, 1);
  auto *J2 = new Loop("j2", 60, 1);

  p->addStmt(I);
  I->addStmt(J1);
  I->addStmt(J2);

  auto *ldB = new LoadExpr(B, {new IndexExpr(I), new IndexExpr(J1)});
  auto *st1 =
      new StoreStmt(A, {new IndexExpr(I), new IndexExpr(J1)}, ldB, false);
  J1->addStmt(st1);

  auto *ldC = new LoadExpr(C, {new IndexExpr(I), new IndexExpr(J2)});
  auto *st2 =
      new StoreStmt(A, {new IndexExpr(I), new IndexExpr(J2)}, ldC, false);
  J2->addStmt(st2);

  p->verify();
  p->dump();
  ::peelLoop(J1, 15);
  p->dump();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 8);
  EXPECT_EQ(counter.expr, 19);
  delete p->clone();
  delete p;
}

TEST(basic, vectorize_memcpy_loop) {
  // DEST[i] = SRC[i];
  Program *p = new Program();
  auto *dest = p->addArgument("DEST", {1024}, {"len"}, ElemKind::Float32Ty);
  auto *src = p->addArgument("SRC", {1024}, {"len"}, ElemKind::Float32Ty);

  auto *I = new Loop("i", 1024, 1);

  p->addStmt(I);

  auto *ld = new LoadExpr(src, {new IndexExpr(I)});
  auto *st = new StoreStmt(dest, {new IndexExpr(I)}, ld, false);
  I->addStmt(st);

  p->verify();
  p->dump();
  auto res = ::vectorize(I, 4);
  EXPECT_TRUE(res);
  EXPECT_EQ(I->getStride(), 4);

  p->dump();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 3);
  EXPECT_EQ(counter.expr, 3);
  delete p->clone();
  delete p;
}

TEST(basic, vectorize_memset) {
  // DEST[i] = 0;
  Program *p = new Program();
  auto *dest = p->addArgument("DEST", {125}, {"len"}, ElemKind::Float32Ty);
  auto *I = new Loop("i", 125, 1);
  p->addStmt(I);

  auto *st =
      new StoreStmt(dest, {new IndexExpr(I)}, new ConstantFPExpr(0.1), false);
  I->addStmt(st);

  p->dump();
  auto res = ::vectorize(I, 8);
  EXPECT_TRUE(res);
  EXPECT_EQ(I->getStride(), 8);
  p->dump();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 5);
  EXPECT_EQ(counter.expr, 7);
  delete p->clone();
  delete p;
}

TEST(basic, widen_loop) {
  Program *p = new Program();
  p->addArgument("D", {17}, {"D"}, ElemKind::Float32Ty);
  auto *D = p->getArg(0);

  auto *I = new Loop("i", 17, 1);
  p->addStmt(I);
  auto *st1 = new StoreStmt(D, {new IndexExpr(I)}, new ConstantFPExpr(0.2), 0);
  I->addStmt(st1);

  p->verify();
  p->dump();
  ::widen(I, 3);
  EXPECT_EQ(I->getStride(), 3);
  p->dump();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 7);
  EXPECT_EQ(counter.expr, 14);
  delete p->clone();
  delete p;
}

TEST(basic, vectorize_widen_loop) {
  Program *p = new Program();
  auto *K = p->addArgument("K", {117}, {"K"}, ElemKind::Float32Ty);

  auto *I = new Loop("index", 117);
  p->addStmt(I);
  auto *st1 = new StoreStmt(K, {new IndexExpr(I)}, new ConstantFPExpr(33), 1);
  I->addStmt(st1);

  p->verify();
  p->dump();
  ::vectorize(I, 4);
  EXPECT_EQ(I->getStride(), 4);

  p->verify();
  p->dump();
  ::widen(I, 3);
  EXPECT_EQ(I->getStride(), 12);
  p->dump();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 9);
  EXPECT_EQ(counter.expr, 22);
  delete p->clone();
  delete p;
}

TEST(basic, simplify_program) {
  Program *p = new Program();
  auto *K = p->addArgument("K", {117}, {"K"}, ElemKind::Float32Ty);

  // Loop from zero to one.
  auto *I = new Loop("index", 1);
  p->addStmt(I);

  p->addStmt(new Loop("index2", 10));
  p->addStmt(new Loop("index3", 12));
  p->addStmt(new Loop("index4", 13));

  auto *st1 = new StoreStmt(K, {new IndexExpr(I)}, new ConstantFPExpr(33), 1);
  I->addStmt(st1);

  p->verify();
  p->dump();
  ::simplify(p);
  p->dump();
  p->verify();

  NodeCounter counter;
  p->visit(&counter);

  EXPECT_EQ(counter.stmt, 2);
  EXPECT_EQ(counter.expr, 2);
  delete p->clone();
  delete p;
}

// Check that we can build a simple program with local vars.
TEST(basic, local_vars) {
  Program *p = new Program();
  auto *A = p->addArgument("A", {32, 32}, {"X", "Y"}, ElemKind::Float32Ty);
  auto *loc = p->addLocalVar("local1", ExprType(ElemKind::Float32Ty));

  EXPECT_EQ(loc, p->getVar("local1"));

  auto *ld = new LoadExpr(A, {new ConstantExpr(0), new ConstantExpr(0)});
  auto *save = new StoreLocalStmt(loc, ld, 0);
  auto *restore = new LoadLocalExpr(loc);
  auto *store = new StoreStmt(A, {new ConstantExpr(0), new ConstantExpr(0)},
                              restore, true);
  p->addStmt(save);
  p->addStmt(store);
  p->dump();
  Program *pp = p->clone();
  delete p;
  pp->dump();
  delete pp;
}

TEST(basic, hois_loads) {
  Program *p = new Program();
  auto *K = p->addArgument("K", {1}, {"K"}, ElemKind::Float32Ty);
  auto *T = p->addArgument("T", {256}, {"T"}, ElemKind::Float32Ty);

  auto *I = new Loop("index", 256);
  p->addStmt(I);

  auto *ld = new LoadExpr(K, {new ConstantExpr(0)});
  auto *st1 = new StoreStmt(T, {new IndexExpr(I)}, ld, false);
  I->addStmt(st1);

  p->verify();
  p->dump();
  ::promoteLICM(p, I);
  p->dump();
  p->verify();

  NodeCounter counter;
  p->visit(&counter);
  EXPECT_EQ(counter.stmt, 4);
  EXPECT_EQ(counter.expr, 4);
  delete p->clone();
  delete p;
}

TEST(basic, sink_stores) {
  Program *p = new Program();
  auto *K = p->addArgument("K", {256}, {"K"}, ElemKind::Float32Ty);
  auto *T = p->addArgument("T", {1}, {"T"}, ElemKind::Float32Ty);

  auto *I = new Loop("index", 256);
  p->addStmt(I);

  auto *ld = new LoadExpr(K, {new IndexExpr(I)});
  auto *st1 = new StoreStmt(T, {new ConstantExpr(0)}, ld, false);
  I->addStmt(st1);

  p->verify();
  p->dump();
  ::promoteLICM(p, I);
  p->dump();
  p->verify();

  NodeCounter counter;
  p->visit(&counter);
  EXPECT_EQ(counter.stmt, 5);
  EXPECT_EQ(counter.expr, 5);
  delete p->clone();
  delete p;
}
