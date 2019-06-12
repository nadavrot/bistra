#include "bistra/Analysis/Value.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Parser/Lexer.h"
#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include "gtest/gtest.h"

using namespace bistra;

TEST(basic, vectorized_loads) {
  const char *vectorized_loads = R"(
  func vectorized_loads(A:float<x:10>, B:float<x:10>) {
    #vectorize 4
    for (i in 0 .. A.x) {
      B[i] = A[i] + 10.0
    }
  })";

  ParserContext ctx(vectorized_loads);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *prog = ctx.getProgram();
  prog->dump();

  float data[20] = {
      1.0, 2.0, 3.0, 4.0, 5.0, 1.0, 2.0, 3.0, 4.0, 5.0,
  };

  auto backend = getBackend("llvm");
  backend->runOnce(prog, data);

  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(data[10 + i], data[i] + 10.0);
  }
}
