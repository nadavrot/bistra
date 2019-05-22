#include "bistra/Analysis/Visitors.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"
#include "bistra/Transforms/Transforms.h"

#include "gtest/gtest.h"

using namespace bistra;

auto loc = DebugLoc::npos();

Program *generateGemm(unsigned szI, unsigned szJ, unsigned szK) {
  // C[i, j] = A[i, k] * B[k, j];
  Program *p = new Program("gemm", loc);
  auto *C = p->addArgument("C", {szI, szK}, {"I", "J"}, ElemKind::Float32Ty);
  auto *A = p->addArgument("A", {szI, szJ}, {"I", "K"}, ElemKind::Float32Ty);
  auto *B = p->addArgument("B", {szJ, szK}, {"K", "J"}, ElemKind::Float32Ty);

  auto *I = new Loop("i", loc, szI, 1);
  auto *J = new Loop("j", loc, szJ, 1);
  auto *K = new Loop("k", loc, szK, 1);
  auto *zero = new StoreStmt(C, {new IndexExpr(I), new IndexExpr(J)},
                             new ConstantFPExpr(0), false, loc);
  p->addStmt(I);
  I->addStmt(J);
  J->addStmt(zero);
  J->addStmt(K);

  auto *ldA = new LoadExpr(A, {new IndexExpr(I), new IndexExpr(K)}, loc);
  auto *ldB = new LoadExpr(B, {new IndexExpr(K), new IndexExpr(J)}, loc);
  auto *mul = new BinaryExpr(ldA, ldB, BinaryExpr::BinOpKind::Mul, loc);
  auto *st =
      new StoreStmt(C, {new IndexExpr(I), new IndexExpr(J)}, mul, true, loc);
  K->addStmt(st);
  return p;
}

// Check that we can build a simple program.
TEST(basic, simple_builder) {
  Program *p = generateGemm(1024, 256, 128);
  p->dump();
  delete p;
}

// Check that we can build a simple program, clone a graph and dump it.
TEST(basic, builder) {
  Program *p = new Program("test", loc);
  p->addArgument("bar", {32, 32}, {"X", "Y"}, ElemKind::Float32Ty);
  p->addArgument("input", {32, 32}, {"X", "Y"}, ElemKind::Float32Ty);
  p->addArgument("foo", {10, 32, 32, 4}, {"N", "H", "W", "C"},
                 ElemKind::Float32Ty);
  auto *L = new Loop("i", loc, 10, 1);
  auto *K = new Loop("j", loc, 10, 1);
  L->addStmt(K);
  p->addStmt(L);

  auto *A = p->getArg(0);
  auto *B = p->getArg(1);

  auto *ld = new LoadExpr(A, {new IndexExpr(K), new IndexExpr(L)}, loc);
  auto *val = new BinaryExpr(ld, new ConstantFPExpr(4),
                             BinaryExpr::BinOpKind::Add, loc);
  auto *store =
      new StoreStmt(B, {new IndexExpr(K), new IndexExpr(L)}, val, true, loc);
  K->addStmt(store);
  p->dump();

  // Check the ownership of the nodes in the graph.
  EXPECT_EQ(L->getParent(), p);
  EXPECT_EQ(K->getParent(), L);
  EXPECT_EQ(store->getParent(), K);
  EXPECT_EQ(ld->getParent(), val);
  EXPECT_EQ(val->getParent(), store);

  Program *pp = p->clone();
  delete p;
  pp->dump();
  delete pp;
}

TEST(basic, matmul) {
  // C[i, j] = A[i, k] * B[k, j];
  Program *p = new Program("simple", loc);
  p->addArgument("C", {128, 32}, {"I", "J"}, ElemKind::Float32Ty);
  p->addArgument("A", {128, 64}, {"I", "K"}, ElemKind::Float32Ty);
  p->addArgument("B", {64, 32}, {"K", "J"}, ElemKind::Float32Ty);

  auto *C = p->getArg(0);
  auto *B = p->getArg(1);
  auto *A = p->getArg(2);

  auto *I = new Loop("i", loc, 128, 1);
  auto *J = new Loop("j", loc, 32, 1);
  auto *K = new Loop("k", loc, 64, 1);

  p->addStmt(I);
  I->addStmt(J);
  J->addStmt(K);

  auto *ldB = new LoadExpr(B, {new IndexExpr(I), new IndexExpr(K)}, loc);
  auto *ldA = new LoadExpr(A, {new IndexExpr(K), new IndexExpr(J)}, loc);
  auto *mul = new BinaryExpr(ldA, ldB, BinaryExpr::BinOpKind::Mul, loc);
  auto *st =
      new StoreStmt(C, {new IndexExpr(I), new IndexExpr(J)}, mul, true, loc);
  K->addStmt(st);

  p->verify();

  Program *pp = p->clone();
  delete p;
  pp->dump();
  delete pp;
}

TEST(basic, memcpy) {
  // DEST[i] = SRC[i];
  Program *p = new Program("memcpy", loc);
  auto *dest = p->addArgument("DEST", {256}, {"len"}, ElemKind::Float32Ty);
  auto *src = p->addArgument("SRC", {256}, {"len"}, ElemKind::Float32Ty);

  auto *I = new Loop("i", loc, 256, 1);

  p->addStmt(I);

  auto *ld = new LoadExpr(src, {new IndexExpr(I)}, loc);
  auto *st = new StoreStmt(dest, {new IndexExpr(I)}, ld, false, loc);
  I->addStmt(st);

  p->verify();
  Program *pp = p->clone();
  delete p;
  pp->dump();
  delete pp;
}

TEST(basic, visitor_collect_indices) {
  // C[i, j] = A[i, k] * B[k, j];
  Program *p = new Program("gemm", loc);
  p->addArgument("C", {128, 256}, {"I", "J"}, ElemKind::Float32Ty);
  p->addArgument("A", {128, 512}, {"I", "K"}, ElemKind::Float32Ty);
  p->addArgument("B", {512, 256}, {"K", "J"}, ElemKind::Float32Ty);

  auto *C = p->getArg(0);
  auto *B = p->getArg(1);
  auto *A = p->getArg(2);

  auto *I = new Loop("i", loc, 128, 1);
  auto *J = new Loop("j", loc, 32, 1);
  auto *K = new Loop("k", loc, 64, 1);

  p->addStmt(I);
  I->addStmt(J);
  J->addStmt(K);

  auto *ldB = new LoadExpr(B, {new IndexExpr(I), new IndexExpr(K)}, loc);
  auto *ldA = new LoadExpr(A, {new IndexExpr(K), new IndexExpr(J)}, loc);
  auto *mul = new BinaryExpr(ldA, ldB, BinaryExpr::BinOpKind::Mul, loc);
  auto *st =
      new StoreStmt(C, {new IndexExpr(I), new IndexExpr(J)}, mul, true, loc);

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
  Program *p = new Program("gemm", loc);
  p->addArgument("C", {128, 256}, {"I", "J"}, ElemKind::Float32Ty);
  p->addArgument("A", {128, 512}, {"I", "K"}, ElemKind::Float32Ty);
  p->addArgument("B", {512, 256}, {"K", "J"}, ElemKind::Float32Ty);

  auto *C = p->getArg(0);
  auto *B = p->getArg(1);
  auto *A = p->getArg(2);

  auto *I = new Loop("i", loc, 128, 1);
  auto *J = new Loop("j", loc, 32, 1);
  auto *K = new Loop("k", loc, 64, 1);

  p->addStmt(I);
  I->addStmt(J);
  J->addStmt(K);

  auto *ldB = new LoadExpr(B, {new IndexExpr(I), new IndexExpr(K)}, loc);
  auto *ldA = new LoadExpr(A, {new IndexExpr(K), new IndexExpr(J)}, loc);
  auto *mul = new BinaryExpr(ldA, ldB, BinaryExpr::BinOpKind::Mul, loc);
  auto *st =
      new StoreStmt(C, {new IndexExpr(I), new IndexExpr(J)}, mul, true, loc);
  K->addStmt(st);

  auto *ir = new IfRange(new IndexExpr(I), 0, 10, loc);
  I->addStmt(ir);

  p->verify();

  Program *pp = p->clone();
  delete p;
  delete pp;
}

TEST(basic, tile_loop) {
  Program *p = new Program("simple", loc);
  p->addArgument("A", {125}, {"X"}, ElemKind::Float32Ty);
  p->addArgument("B", {125}, {"X"}, ElemKind::Float32Ty);

  auto *B = p->getArg(0);
  auto *A = p->getArg(1);

  auto *I = new Loop("i", loc, 125, 1);

  p->addStmt(I);

  auto *ldB = new LoadExpr(B, {new IndexExpr(I)}, loc);
  auto *cf = new ConstantFPExpr(1.5);
  auto *mul = new BinaryExpr(ldB, cf, BinaryExpr::BinOpKind::Mul, loc);
  auto *st = new StoreStmt(A, {new IndexExpr(I)}, mul, true, loc);
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
  Program *p = new Program("unroll_me", loc);
  p->addArgument("A", {10}, {"X"}, ElemKind::Float32Ty);

  auto *A = p->getArg(0);

  auto *I = new Loop("i", loc, 10, 1);
  p->addStmt(I);
  auto *st1 =
      new StoreStmt(A, {new IndexExpr(I)}, new ConstantFPExpr(0.1), false, loc);
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
  Program *p = new Program("mem_cpy", loc);
  auto *dest = p->addArgument("DEST", {260}, {"len"}, ElemKind::Float32Ty);
  auto *src = p->addArgument("SRC", {260}, {"len"}, ElemKind::Float32Ty);

  auto *I = new Loop("i", loc, 260, 1);

  p->addStmt(I);

  auto *ld = new LoadExpr(src, {new IndexExpr(I)}, loc);
  auto *st = new StoreStmt(dest, {new IndexExpr(I)}, ld, false, loc);
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
  Program *p = new Program("simple", loc);
  p->addArgument("A", {60, 60}, {"X", "Y"}, ElemKind::Float32Ty);
  p->addArgument("B", {60, 60}, {"X", "Y"}, ElemKind::Float32Ty);
  p->addArgument("C", {60, 60}, {"X", "Y"}, ElemKind::Float32Ty);

  auto *A = p->getArg(0);
  auto *B = p->getArg(1);
  auto *C = p->getArg(2);

  auto *I = new Loop("i", loc, 60, 1);
  auto *J1 = new Loop("j1", loc, 60, 1);
  auto *J2 = new Loop("j2", loc, 60, 1);

  p->addStmt(I);
  I->addStmt(J1);
  I->addStmt(J2);

  auto *ldB = new LoadExpr(B, {new IndexExpr(I), new IndexExpr(J1)}, loc);
  auto *st1 =
      new StoreStmt(A, {new IndexExpr(I), new IndexExpr(J1)}, ldB, false, loc);
  J1->addStmt(st1);

  auto *ldC = new LoadExpr(C, {new IndexExpr(I), new IndexExpr(J2)}, loc);
  auto *st2 =
      new StoreStmt(A, {new IndexExpr(I), new IndexExpr(J2)}, ldC, false, loc);
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
  Program *p = new Program("memcpy", loc);
  auto *dest = p->addArgument("DEST", {1024}, {"len"}, ElemKind::Float32Ty);
  auto *src = p->addArgument("SRC", {1024}, {"len"}, ElemKind::Float32Ty);

  auto *I = new Loop("i", loc, 1024, 1);

  p->addStmt(I);

  auto *ld = new LoadExpr(src, {new IndexExpr(I)}, loc);
  auto *st = new StoreStmt(dest, {new IndexExpr(I)}, ld, false, loc);
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
  Program *p = new Program("memset", loc);
  auto *dest = p->addArgument("DEST", {125}, {"len"}, ElemKind::Float32Ty);
  auto *I = new Loop("i", loc, 125, 1);
  p->addStmt(I);

  auto *st = new StoreStmt(dest, {new IndexExpr(I)}, new ConstantFPExpr(0.1),
                           false, loc);
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
  Program *p = new Program("simple", loc);
  p->addArgument("D", {17}, {"D"}, ElemKind::Float32Ty);
  auto *D = p->getArg(0);

  auto *I = new Loop("i", loc, 17, 1);
  p->addStmt(I);
  auto *st1 =
      new StoreStmt(D, {new IndexExpr(I)}, new ConstantFPExpr(0.2), 0, loc);
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
  Program *p = new Program("simple", loc);
  auto *K = p->addArgument("K", {117}, {"K"}, ElemKind::Float32Ty);

  auto *I = new Loop("index", loc, 117);
  p->addStmt(I);
  auto *st1 =
      new StoreStmt(K, {new IndexExpr(I)}, new ConstantFPExpr(33), 1, loc);
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
  Program *p = new Program("simple", loc);
  auto *K = p->addArgument("K", {117}, {"K"}, ElemKind::Float32Ty);

  // Loop from zero to one.
  auto *I = new Loop("index", loc, 1);
  p->addStmt(I);

  p->addStmt(new Loop("index2", loc, 10));
  p->addStmt(new Loop("index3", loc, 12));
  p->addStmt(new Loop("index4", loc, 13));

  auto *st1 =
      new StoreStmt(K, {new IndexExpr(I)}, new ConstantFPExpr(33), 1, loc);
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
  Program *p = new Program("simple", loc);
  auto *A = p->addArgument("A", {32, 32}, {"X", "Y"}, ElemKind::Float32Ty);
  auto *local = p->addLocalVar("local1", ExprType(ElemKind::Float32Ty));

  EXPECT_EQ(local, p->getVar("local1"));

  auto *ld = new LoadExpr(A, {new ConstantExpr(0), new ConstantExpr(0)}, loc);
  auto *save = new StoreLocalStmt(local, ld, 0, loc);
  auto *restore = new LoadLocalExpr(local, loc);
  auto *store = new StoreStmt(A, {new ConstantExpr(0), new ConstantExpr(0)},
                              restore, true, loc);
  p->addStmt(save);
  p->addStmt(store);
  p->dump();
  Program *pp = p->clone();
  delete p;
  pp->dump();
  delete pp;
}

TEST(basic, hois_loads) {
  Program *p = new Program("simple", loc);
  auto *K = p->addArgument("K", {1}, {"K"}, ElemKind::Float32Ty);
  auto *T = p->addArgument("T", {256}, {"T"}, ElemKind::Float32Ty);

  auto *I = new Loop("index", loc, 256);
  p->addStmt(I);

  auto *ld = new LoadExpr(K, {new ConstantExpr(0)}, loc);
  auto *st1 = new StoreStmt(T, {new IndexExpr(I)}, ld, false, loc);
  I->addStmt(st1);

  p->verify();
  p->dump();
  ::promoteLICM(p);
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
  Program *p = new Program("simple", loc);
  auto *K = p->addArgument("K", {256}, {"K"}, ElemKind::Float32Ty);
  auto *T = p->addArgument("T", {1}, {"T"}, ElemKind::Float32Ty);

  auto *I = new Loop("index", loc, 256);
  p->addStmt(I);

  auto *ld = new LoadExpr(K, {new IndexExpr(I)}, loc);
  auto *st1 = new StoreStmt(T, {new ConstantExpr(0)}, ld, false, loc);
  I->addStmt(st1);

  p->verify();
  p->dump();
  ::promoteLICM(p);
  p->dump();
  p->verify();

  NodeCounter counter;
  p->visit(&counter);
  EXPECT_EQ(counter.stmt, 5);
  EXPECT_EQ(counter.expr, 5);
  delete p->clone();
  delete p;
}
