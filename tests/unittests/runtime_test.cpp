#include "bistra/Analysis/Value.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Transforms.h"

#include "gtest/gtest.h"

using namespace bistra;

TEST(runtime, simple_loop) {
  const char *simple_loop = R"(
  func simple_loop(A:float<x:10>, B:float<x:10>) {
    #vectorize 4
    for (i in 0 .. A.x) {
      B[i] = A[i] + 10.0
    }
  })";

  ParserContext ctx(simple_loop);
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

TEST(runtime, gemm) {
  const char *gemm = R"(
  let m = 512
  let n = 512
  let k = 512
  func gemm(C:float<I:m, J:n>,
            A:float<I:m, K:k>,
            B:float<K:k, J:n>) {
    for (i in 0 .. C.I) {
      for (j in 0 .. C.J) {
        C[i,j] = 0.0;
        for (k in 0 .. A.K) {
          C[i,j] += A[i,k] * B[k,j];
        }
      }
    }
  })";

  ParserContext ctx(gemm);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *prog = ctx.getProgram();

  bool res = ::ditributeAllLoops(prog);
  EXPECT_EQ(res, true);

  auto *J = ::getLoopByName(prog, "j_split_1");
  auto *I = ::getLoopByName(prog, "i_split_1");
  res &= ::vectorize(J, 4);
  res &= ::widen(I, 3);
  EXPECT_EQ(res, true);
  prog->dump();

  int sz = 512 * 512 * 3 * sizeof(float);
  float *data = (float *)malloc(sz);
  memset(data, 0, sz);

#define GET(MAT, X, Y) (data[(512 * 512 * MAT) + (512 * Y) + X])

  // Create a random matrix and the identity matrix.
  for (int i = 0; i < 512; i++) {
    for (int j = 0; j < 512; j++) {
      GET(1, i, j) = (i == j);
      GET(2, i, j) = float(i % 5 + j % 5 - 5);
    }
  }

  auto backend = getBackend("llvm");
  backend->runOnce(prog, data);

  for (int i = 0; i < 512; i++) {
    for (int j = 0; j < 512; j++) {
      float valb = GET(2, i, j);
      float valc = GET(0, i, j);
      EXPECT_EQ(valb, valc);
    }
  }

  free(data);
#undef GET
}
