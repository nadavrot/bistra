#include "bistra/Parser/Parser.h"

#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Transforms.h"

#include <iostream>

using namespace bistra;

const char *gemmSource = R"(
def gemm (C:float<I:512,J:512>, A:float<I:512,K:512>, B:float<K:512,J:512>) {
  for (i in 0 .. 512) {
    for (j in 0 .. 512) {
      C[i,j] =  0.000000 ;
      for (k in 0 .. 512) {
        C[i,j] += A[i,k] * B[k,j];
      }
    }
  }
}
})";

int main() {
  Program *p = parseProgram(gemmSource);
  optimizeEvaluate(p);
}
