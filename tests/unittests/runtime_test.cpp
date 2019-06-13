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

TEST(runtime, basic_printing) {
  const char *basic_printing = R"(
  func basic_printing(A:float<x:10>) {
    printf("basic_printing test\n")
    for (i in 0 .. 10) {
      printf("%d : %f\n", i, A[i])
    }
  })";

  ParserContext ctx(basic_printing);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *prog = ctx.getProgram();
  prog->dump();

  float data[10] = {
      1.9, 2.8, 3.7, 4.6, 5.7, 6.4, 7.3, 8.2, 9.1,
  };

  auto backend = getBackend("llvm");
  backend->runOnce(prog, data);
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

TEST(runtime, softmax) {
  const char *softmax = R"(
  let size = 7;
  func softmax(In:float<x:size>,
               Out:float<x:size>) {
    var mx : float = In[0]

    // Find Max.
    for (i in 0 .. size) {
      mx = max(mx, In[i]);
    }

    var sum : float = 0.0

    // Compute exp.
    for (i in 0 .. size) {
      var e : float = exp(In[i] - mx)
      sum += e
      Out[i] = e
    }

    // Normalize the output.
    for (i in 0 .. size) {
      Out[i] = Out[i] / sum;
    }
  }
  )";

  ParserContext ctx(softmax);
  Parser P(ctx);
  P.parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *prog = ctx.getProgram();
  prog->dump();

  float data[14] = {1.0, 2.0, 3.0, 4.0, 1.0, 2.0, 3.0,
                    .0,  .0,  .0,  .0,  .0,  .0,  .0};

  float result[7] = {0.024, 0.064, 0.175, 0.475, 0.024, 0.064, 0.175};

  auto backend = getBackend("llvm");
  backend->runOnce(prog, data);

  for (int i = 0; i < 7; i++) {
    EXPECT_NEAR(data[7 + i], result[i], 0.001);
  }
}
