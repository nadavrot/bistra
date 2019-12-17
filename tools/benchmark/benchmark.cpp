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
#include <sstream>

using namespace bistra;

const char *gemmSource = R"(
func gemm (C:float<I:szI,J:szJ>, A:float<I:szI,K:szK>, B:float<K:szK,J:szJ>) {
  for (i in 0 .. C.I) {
    for (j in 0 .. C.J) {
      C[i,j] = 0.0;
      for (k in 0 .. A.K) {
        C[i,j] += A[i,k] * B[k,j];
      }
    }
  }
}

script for "x86" {
  widen "i" to 4
  vectorize "j" to 8 as "j8"
  widen "j8" to 3 as "j8_3"
}
)";

const char *batchedAddSource = R"(
func batched_add(Out:float<x:sx, y:sy>, In:float<b:batch, x:sx, y:sy>) {
  for (x in 0 .. In.x) {
    for (y in 0 .. In.y) {
      Out[x,y] = 0.0
        for (b in 0 .. In.b) {
          Out[x,y] += In[b,x,y]
        }
    }
  }
}
)";

const char *transposeSource = R"(
func transpose(A:float<width:sx, height:sy>,
               B:float<height:sy, width:sx>) {
  for (i in 0 .. A.height) {
    for (j in 0 .. A.width) {
      A[i,j] = B[j,i];
    }
  }
}

script for "x86" {
  tile "i" to 64 as "i_tiled"
  tile "j" to 64 as "j_tiled"
  // Reorder the loops as [i, j, i_t, j_t].
  hoist "j" 1 times
}
)";

const char *saxpySource = R"(
func saxpy(Out:float<len:sx>,
           A:float<len:sx>,
           B:float<len:sx>,
           C:float<len:1>) {
  var x : float = C[0]
  for (i in 0 .. Out.len) {
    Out[i] = A[i] *x
  }
 for (i in 0 .. Out.len) {
   Out[i] += Out[i] + B[i]
 }
}

)";

const char *maxpool2dSource = R"(
let size_in = size_out * stride + kernel

func maxpool2d(Out: float<N:batch, H:size_out, W:size_out, C:channels>,
               In: float<N:batch, H:size_in, W:size_in, C:channels>) {

  for (n in 0 .. Out.N) {
    // For each output channel:
    for (c in 0 .. Out.C) {
      // For each pixel in the output buffer.
      for (outx in 0 .. Out.W) {
        for (outy in 0 .. Out.H) {
          // Identify the start coordinate of the max region.
          let inX = outx * stride;
          let inY = outy * stride;
          // Init the output buffer.
          Out[n, outx, outy, c] = -999.0

            // For each element in the filter:
            for (fx in 0 .. kernel) {
              for (fy in 0 .. kernel) {
                let px = inX + fx;
                let py = inY + fy;
                Out[n, outx, outy, c] = max(In[n, px, py, c], Out[n, outx, outy, c]);
              } // FY
            } // FX
        } // OY
      } // OX
    } // C
  } // N
}
)";

const char *batchnormSource = R"(
let epsilon = 0.001

func batchnorm(
  Out:   float<N:batch, H:hw, W:hw, C:channel>,
  In:    float<N:batch, H:hw, W:hw, C:channel>,
  Mean:  float<C:channel>,
  Var:   float<C:channel>,
  Gamma: float<C:channel>,
  Beta:  float<C:channel>) {

  for (n in 0 .. batch) {
    for (x in 0 .. hw) {
      for (y in 0 .. hw) {
        for (c in 0 .. channel) {
            let input = In[n, x, y, c]
            let mu = Mean[c]
            let varr = Var[c]
            let gamma = Gamma[c]
            let beta = Beta[c]
            let stdvar = 1.0 / sqrt(varr + epsilon)
            Out[n, x, y, c] = (input - mu) * gamma * stdvar + beta;
        }
      }
    }
  }
}
)";

const char *concatSource = R"(
let sx2 = sx * 2
func concat(O:float<width:sx2, height:sy>,
            A:float<width:sx, height:sy>,
            B:float<height:sy, width:sx>) {
  for (i in 0 .. A.height) {
    for (j in 0 .. A.width) {
      O[i,j] = A[j,i]
    }
  }
  for (i in 0 .. B.height) {
    for (j in 0 .. B.width) {
      O[i + sx, j] = B[j,i]
    }
  }
}
)";

void parseOptimizeAndRun(std::stringstream &report, const char *src,
                         const std::vector<std::string> &letNames,
                         const std::vector<int> &letValues) {
  auto BE = getBackend("llvm");

  ParserContext ctx(src);
  parseProgram(ctx, letNames, letValues);

  assert(!ctx.getNumErrors() && "Unable to parse program");
  Program *program = ctx.getProgram();

  // Apply the pragma commands.
  for (auto &pc : ctx.getPragmaDecls()) {
    bool res = applyPragmaCommand(program, pc);
    assert(res && "Unable to apply pragma");
  }

  auto np = ::optimizeStatic(BE.get(), program);
  np->dump();
  auto timeSec = BE->evaluateCode(np.get(), 10);
  report << timeSec << ", " << program->getName() << "\n";
}

int main() {
  std::stringstream report;
  parseOptimizeAndRun(report, gemmSource, {"szI", "szJ", "szK"},
                      {1024, 1024, 512});
  parseOptimizeAndRun(report, batchedAddSource, {"sx", "sy", "batch"},
                      {512, 1024, 64});
  parseOptimizeAndRun(report, transposeSource, {"sx", "sy"}, {2048, 2048});
  parseOptimizeAndRun(report, saxpySource, {"sx"}, {1024 * 1024 * 10});
  parseOptimizeAndRun(report, maxpool2dSource,
                      {"kernel", "stride", "batch", "channels", "size_out"},
                      {3, 2, 16, 128, 64});
  parseOptimizeAndRun(report, batchnormSource, {"batch", "channel", "hw"},
                      {32, 128, 128});
  parseOptimizeAndRun(report, concatSource, {"sx", "sy"}, {1024, 2048});

  std::cout << "-- report -- \n" << report.str() << "\n";
}
