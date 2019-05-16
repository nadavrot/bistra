#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Transforms.h"

#include "gtest/gtest.h"

using namespace bistra;

TEST(opt, tiler) {
  const char *tiler = R"(
  def tiler(C:float<x:510>) {
    for (i in 0 .. C.x) { C[i] = 19.0 }
  })";

  ParserContext ctx;
  Parser P(tiler, ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *p = ctx.getProgram();
  p->dump();

  Loop *I = ::getLoopByName(p, "i");
  ::widen(I, 3);
  EXPECT_EQ(I->getStride(), 3);
  ::tile(I, 33);

  p->dump();
}

TEST(opt, split_loop) {
  const char *code = R"(
  def split_me(A:float<x:100>, B:float<x:100>) {
    for (i in 0 .. A.x) { A[i] = 0.0; B[i] = 1.0 }
  })";

  ParserContext ctx;
  Parser P(code, ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *p = ctx.getProgram();
  Loop *I = ::getLoopByName(p, "i");
  ::split(I);

  p->dump();

  NodeCounter counter;
  p->visit(&counter);
  EXPECT_EQ(counter.stmt, 5);
  EXPECT_EQ(counter.expr, 4);
}
