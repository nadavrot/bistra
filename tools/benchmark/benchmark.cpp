#include "bistra/Analysis/Value.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"
#include "bistra/Transforms/Transforms.h"

#include <iostream>

using namespace bistra;

const char *gemmSource = R"(
func gemm (C:float<I:szI,J:szJ>, A:float<I:szI,K:szK>, B:float<K:szK,J:szJ>) {
  for (i in 0 .. C.I) {
    for (j in 0 .. C.J) {
      C[i,j] =  0.0
      for (k in 0 .. A.K) {
        C[i,j] += A[i,k] * B[k,j]
      }
    }
  }
}

script for "x86" {
  distribute "i"
  vectorize "j_split_1" to 8
  tile "i_split_1" to 16 as "j_tiled"
}

)";

Program *parseAndOptimize(const char *src,
                          const std::vector<std::string> &letNames,
                          const std::vector<int> &letValues) {
  ParserContext ctx(src);
  parseProgram(ctx, letNames, letValues);

  assert(!ctx.getNumErrors() && "Unable to parse program");
  Program *program = ctx.getProgram();

  // Apply the pragma commands.
  for (auto &pc : ctx.getPragmaDecls()) {
    bool res = applyPragmaCommand(program, pc);
    assert(res && "Unable to apply pragma");
  }
  return program;
}

int main() {
  Program *p =
      parseAndOptimize(gemmSource, {"szI", "szJ", "szK"}, {512, 512, 512});
  ::simplify(p);
  ::promoteLICM(p);
  ::simplify(p);
  p->dump();

  auto CB = getBackend("llvm");
  auto timeSec = CB->evaluateCode(p, 10);
  std::cout << "result = " << timeSec << "\n";
}
