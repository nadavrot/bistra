#include "bistra/Analysis/Value.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"
#include "bistra/Transforms/Transforms.h"

#include <string>
#include <iostream>

using namespace bistra;

const char *gemmSource = R"(
let sz = 512;
func gemm (C:float<I:sz,J:sz>, A:float<I:sz,K:sz>, B:float<K:sz,J:sz>) {
  for (i in 0 .. C.I) {
    for (j in 0 .. C.J) {
      C[i,j] =  0.000000 ;
      for (k in 0 .. A.K) {
        C[i,j] += A[i,k] * B[k,j];
      }
    }
  }
}

script for "x86" {
  vectorize "i" to 4
  tile "j" to 16 as "j_tiled"
}

)";

int main() {
  Program *p = parseProgram(gemmSource);
  auto *I = ::getLoopByName(p, "i");
  auto *J = ::getLoopByName(p, "j");
  auto *K = ::getLoopByName(p, "k");

  ::vectorize(J, 8);
  ::widen(J, 4);
  ::widen(I, 3);

  ::tile(K, 32);
  ::hoist(K, 3);

  ::tile(I, 63);
  ::hoist(I, 3);

  ::simplify(p);
  ::promoteLICM(p);
  ::simplify(p);

  p->dump();

  auto CB = getBackend("llvm");
  CB->emitProgramCode(p, "/tmp/1.cc", true, 10);

  auto res = CB->evaluateCode(p, 10);
  std::cout << "result = " << res << "\n";
}
