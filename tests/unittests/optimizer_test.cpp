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
