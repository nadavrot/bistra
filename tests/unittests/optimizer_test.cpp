#include "bistra/Analysis/Value.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"
#include "bistra/Transforms/Transforms.h"

#include "gtest/gtest.h"

using namespace bistra;

TEST(opt, tiler) {
  const char *tiler = R"(
  func tiler(C:float<x:510>) {
    for (i in 0 .. C.x) { C[i] = 19.0 }
  })";

  ParserContext ctx(tiler);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *p = ctx.getProgram();
  Loop *I = ::getLoopByName(p, "i");
  ::widen(I, 3);
  EXPECT_EQ(I->getStride(), 3);
  ::tile(I, 33);

  p->dump();
}

TEST(opt, split_loop) {
  const char *code = R"(
  func split_me(A:float<x:100>, B:float<x:100>) {
    for (i in 0 .. A.x) { A[i] = 0.0; B[i] = 1.0 }
  })";

  ParserContext ctx(code);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *p = ctx.getProgram();
  p->dump();
  ::simplify(p);
}

TEST(opt, simplifyExpr) {
  const char *code = R"(
  func simplifyExpr(A:float<x:100>, B:float<x:100>) {
    A[0] = 4.0 + 5.0
    A[1] = B[0] * 0.0 + B[1 * 3] + 0.0 + B[0 + 2] * 1.0
    for (i in 0 .. 24) {
      A[(2 * 2)] = B[(0 + 1)] + 2.0 + B[(2 * i)] + 1.0
    }
    if ((1 + 32) in 0 .. 34) { A[3 + 0] = 3.0 + 34.0 }
  })";

  ParserContext ctx(code);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *p = ctx.getProgram();
  ::simplify(p);
  p->dump();

  NodeCounter counter;
  p->visit(&counter);
  EXPECT_EQ(counter.stmt, 6);
  EXPECT_EQ(counter.expr, 30);
}

TEST(opt, range_check_loops) {
  const char *code = R"(
  func range_check_loops(A:float<x:100>, B:float<x:100>) {
    for (i in 0 .. 100) {
      if ((i * 2 )     in 0 .. 300) { A[1] = 0.0 } // keep
      if ((i * 2 + 50) in 0 .. 40 ) { A[2] = 1.0 } // kill
    }

    if ((25 + 25) in 0 .. 40) { A[3] = 1.0 } // kill
    if ((25 + 25) in 0 .. 90) { A[4] = 1.0 } // Inlined.
  })";

  ParserContext ctx(code);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *p = ctx.getProgram();
  ::simplify(p);
  p->dump();

  NodeCounter counter;
  p->visit(&counter);
  EXPECT_EQ(counter.stmt, 8);
  EXPECT_EQ(counter.expr, 18);
}

TEST(opt, sink_loop) {
  const char *code = R"(
  func sink_loop(A:float<x:100, y:100, z:100>, B:float<x:100, y:100, z:100>) {
    for (i in 0 .. 100) {
      for (j in 0 .. 100) {
        for (k in 0 .. 100) {
        A[i,j, k] = A[i,j,k]
        }
      }
    }
  })";

  ParserContext ctx(code);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *p = ctx.getProgram();
  Loop *I = ::getLoopByName(p, "i");

  bool res = ::sink(I, 2);
  p->dump();

  EXPECT_EQ(res, true);
}

TEST(opt, fuse_test) {
  const char *code = R"(
  func fuse_test(A:float<x:100, y:100, z:100>, B:float<x:100, y:100, z:100>) {
    for (i in 0 .. 100) {
      for (j in 0 .. 100) {
        for (k in 0 .. 100) {
          A[i, j, k] += 1;
        }
      }
    }

    for (i1 in 0 .. 100) {
      for (j1 in 0 .. 100) {
        for (k1 in 0 .. 100) {
          B[i1, j1, k1] += 4;
        }
      }
    }
  })";

  ParserContext ctx(code);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *p = ctx.getProgram();
  Loop *I = ::getLoopByName(p, "i");

  // Fuse all loops.
  bool res = ::fuse(I, 3);
  p->dump();
  EXPECT_EQ(res, true);

  NodeCounter counter;
  p->visit(&counter);
  EXPECT_EQ(counter.stmt, 6);
  EXPECT_EQ(counter.expr, 10);
}

TEST(opt, change_layout_test) {
  const char *code = R"(
  let m = 512
  let n = 256
  func transpose(A:float<m:m, n:n>,
                 B:float<n:n, m:m>) {
    for (i in 0 .. A.m) {
      for (j in 0 .. A.n) {
        A[i,j] = B[j,i];
      }
    }
  }
  )";

  ParserContext ctx(code);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *p = ctx.getProgram();
  // Change the layout of the second loop.
  bool res = ::changeLayout(p, 0, {1, 0});
  p->dump();
  EXPECT_EQ(res, true);

  p->verify();
}
