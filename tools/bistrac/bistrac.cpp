#include "bistra/Analysis/Program.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"
#include "bistra/Transforms/Transforms.h"

#define STRIP_FLAG_HELP 0
#include "gflags/gflags.h"

#include <fstream>
#include <iostream>

using namespace bistra;

DEFINE_bool(dump, false, "Dump the texttual representation of the program.");
DEFINE_bool(stats, false, "Dump the roofline model stats for the program.");
DEFINE_bool(opt, false, "Optimize the program.");
DEFINE_bool(tune, false, "Executes and auto-tune the program.");
DEFINE_bool(time, false, "Executes and times the program.");
DEFINE_string(out, "", "Output destination file to save the compiled program.");

/// \returns the containing loop or nullptr.
Loop *getContainingLoop(Stmt *s) {
  ASTNode *p = s;
  while (p) {
    p = p->getParent();
    if (Loop *L = dynamic_cast<Loop *>(p))
      return L;
  }
  return nullptr;
}

void warnIfLoopNotProperlyTiled(Loop *L, ParserContext &ctx) {
  Loop *PL = getContainingLoop(L);
  if (!PL)
    return;

  auto IOL = getNumLoadsInLoop(L);
  auto IOPL = getNumLoadsInLoop(PL);

  if (IOL < 1024 * 4 && (IOPL / IOL) > 50 && IOPL > 1024 * 32) {
    std::string message = "consider tiling a loop that touches " +
                          prettyPrintNumber(IOPL) + " elements";
    ctx.diagnose(ParserContext::DiagnoseKind::Warning, PL->getLoc(), message);
    ctx.diagnose(ParserContext::DiagnoseKind::Note, L->getLoc(),
                 "possible inner loop candidate is here");
  }
}

void analyzeProgram(Program *p, ParserContext &ctx) {
  std::unordered_map<ASTNode *, ComputeCostTy> heatmap;
  estimateCompute(p, heatmap);
  assert(heatmap.count(p) && "No information for the program");
  auto info = heatmap[p];

  std::string message = "the program performs " + std::to_string(info.second) +
                        " arithmetic ops and " + std::to_string(info.first) +
                        " memory ops";
  ctx.diagnose(ParserContext::Note, p->getLoc(), message);

  for (auto &L : collectLoops(p)) {
    warnIfLoopNotProperlyTiled(L, ctx);
  }
}

int main(int argc, char *argv[]) {
  gflags::SetUsageMessage("Bistra compiler driver.");
  gflags::SetVersionString("0.0.1");
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc != 2) {
    std::cout << "Usage: bistrac [...] program.txt\n";
    std::cout << "See --help for more details.\n";
    return 0;
  }
  std::string inFile = argv[1];

  gflags::ShutDownCommandLineFlags();

  auto content = readFile(inFile);
  ParserContext ctx(content.c_str(), inFile);
  Parser P(ctx);
  P.Parse();

  // Abort the program if there were any errors.
  if (ctx.getNumErrors() > 0) {
    return 0;
  }

  Program *p = ctx.getProgram();

  // Apply the pragma commands.
  for (auto &pc : ctx.getPragmaDecls()) {
    bool res = applyPragmaCommand(pc);
    if (!res) {
      ctx.diagnose(ParserContext::DiagnoseKind::Error, pc.loc_,
                   "unable to apply the pragma");
    }
  }

  if (FLAGS_tune) {
    std::string outFile = "/tmp/file.cc";
    if (FLAGS_out.size()) {
      outFile = FLAGS_out;
    } else {
      std::cout << "Output flag (--out) is not set. Using the default: "
                << outFile << "\n";
    }

    optimizeEvaluate(p, outFile);
  }

  if (FLAGS_opt) {
    ::simplify(p);
    ::promoteLICM(p);
  }

  if (FLAGS_dump) {
    p->dump();
  }

  if (FLAGS_time) {
    auto CB = getBackend("C");
    auto res = CB->evaluateCode(p, 10);
    std::cout << "The program \"" << p->getName() << "\" completed in " << res
              << " seconds. \n";
  }

  if (FLAGS_stats) {
    analyzeProgram(p, ctx);
  }

  return 0;
}
