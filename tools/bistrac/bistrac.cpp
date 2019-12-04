#include "bistra/Analysis/Program.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Bytecode/Bytecode.h"
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
DEFINE_bool(warn, false, "Print warnings based on program analysis.");
DEFINE_bool(opt, false, "Optimize the program.");
DEFINE_bool(tune, false, "Executes and auto-tune the program.");
DEFINE_bool(time, false, "Executes and times the program.");
DEFINE_bool(textual, false, "Emit the textual representation of the output.");
DEFINE_bool(bytecode, false, "Emit the bytecode representation.");
DEFINE_string(out, "", "Output destination file to save the compiled program.");
DEFINE_string(backend, "llvm", "The backend to use [C/llvm]");

/// \returns the most expensive operation in the program and it's costt.
std::pair<Expr *, uint64_t> getExpensiveOp(Scope *S) {
  std::vector<LoadExpr *> loads;
  std::vector<StoreStmt *> stores;
  collectLoadStores(S, loads, stores);

  // Insert all of the sub loops into the live set.
  std::vector<Loop *> loops = collectLoops(S);
  std::set<Loop *> live(loops.begin(), loops.end());

  uint64_t mx = 0;
  Expr *mxE = nullptr;

  for (auto *ld : loads) {
    auto io = getAccessedMemoryForSubscript(ld->getIndices(), &live);
    if (mx < io) {
      mx = io;
      mxE = ld;
    }
  }

  for (auto *st : stores) {
    auto io = getAccessedMemoryForSubscript(st->getIndices(), &live);
    if (mx < io) {
      mx = io;
      mxE = st->getValue();
    }
  }

  return {mxE, mx};
}

void warnIfLoopNotProperlyTiled(Loop *L, ParserContext &ctx) {
  Loop *PL = getContainingLoop(L);
  if (!PL)
    return;

  auto IOL = getNumLoadsInLoop(L);
  auto IOPL = getNumLoadsInLoop(PL);

  if (IOL < 1024 * 16 && IOPL > 1024 * 64) {
    std::string message = "consider tiling a loop that touches " +
                          prettyPrintNumber(IOPL) + " elements";

    ctx.diagnose(ParserContext::DiagnoseKind::Warning, PL->getLoc(), message);
    std::string messageHint =
        "here is a possible inner loop that touches only " +
        prettyPrintNumber(IOL) + " elements";
    ctx.diagnose(ParserContext::DiagnoseKind::Note, L->getLoc(), messageHint);
  }
}

/// \returns True if this expression or statement are guarded behind a range
/// check.
static bool isRangeProtected(ASTNode *n) {
  ASTNode *p = n;
  while (p) {
    p = p->getParent();
    if (dynamic_cast<IfRange *>(p))
      return true;
  }
  return false;
}

/// Emit diagnostics for buffer overflow of load/store operations with the
/// indices \p indices for the buffer of shape \p opType at location \p opLoc.
void detectOverflow(DebugLoc opLoc, const Type *opType,
                    const std::vector<ExprHandle> &indices,
                    ParserContext &ctx) {
  for (int i = 0; i < indices.size(); i++) {
    auto &idx = indices[i];
    std::pair<int, int> range;
    if (!computeKnownIntegerRange(idx.get(), range))
      continue;

    auto bufferSize = opType->getDims()[i];
    if (range.first < 0 || range.second > bufferSize) {
      std::string message =
          "buffer overflow detected at index " + std::to_string(i);
      ctx.diagnose(ParserContext::Warning, opLoc, message);
      std::string rangeMsg =
          "the index range is " + std::to_string(range.first) + " .. " +
          std::to_string(range.second) + ", but the buffer range is 0 .. " +
          std::to_string(bufferSize);
      ctx.diagnose(ParserContext::Note, idx->getLoc(), rangeMsg);
    }
  }
}

/// Emit diagnostics for buffer overflow for all load/stores in the program.
void detectOverflow(Program *p, ParserContext &ctx) {
  std::vector<LoadExpr *> loads;
  std::vector<StoreStmt *> stores;
  collectLoadStores(p, loads, stores);

  // We can't analyze the checks in if-range checks, so don't emit warnings
  // on guarded statements.

  // Analyze loads:
  for (auto *ld : loads) {
    if (isRangeProtected(ld))
      continue;
    detectOverflow(ld->getLoc(), ld->getDest()->getType(), ld->getIndices(),
                   ctx);
  }

  // Analyze stores:
  for (auto *st : stores) {
    if (isRangeProtected(st))
      continue;
    detectOverflow(st->getLoc(), st->getDest()->getType(), st->getIndices(),
                   ctx);
  }
}

void analyzeProgram(Program *p, ParserContext &ctx) {
  std::unordered_map<ASTNode *, ComputeCostTy> heatmap;
  estimateCompute(p, heatmap);
  assert(heatmap.count(p) && "No information for the program");
  auto info = heatmap[p];

  // Report general information //
  std::string message = "the program performs " + std::to_string(info.second) +
                        " arithmetic ops and " + std::to_string(info.first) +
                        " memory ops";
  ctx.diagnose(ParserContext::Note, p->getLoc(), message);

  // Make sure that loops are vectorized //
  auto EC = getExpensiveOp(p);
  if (EC.first) {
    if (EC.first->getType().getWidth() == 1) {
      std::string message = "a hot loop performs " +
                            prettyPrintNumber(EC.second) +
                            " unvectorized operations";
      ctx.diagnose(ParserContext::Warning, EC.first->getLoc(), message);
    }
  }

  // Analyze loop cache utilization //
  for (auto &L : collectLoops(p)) {
    warnIfLoopNotProperlyTiled(L, ctx);
  }

  // Detect and warn on buffer overflows.
  detectOverflow(p, ctx);
}

/// Checks if \p str ends with \p suffix.
static bool endsWith(const std::string &str, const std::string &suffix) {
  if (str.size() < suffix.size())
    return false;
  return 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

Program *parseProgram(ParserContext &ctx) {
  Parser P(ctx);
  P.parse();

  // Abort the program if there were any errors.
  if (ctx.getNumErrors() > 0) {
    return nullptr;
  }

  Program *program = ctx.getProgram();

  // Apply the pragma commands.
  for (auto &pc : ctx.getPragmaDecls()) {
    bool res = applyPragmaCommand(program, pc);
    if (!res) {
      program->dump();
      ctx.diagnose(ParserContext::DiagnoseKind::Error, pc.loc_,
                   "unable to apply the pragma");
      return program;
    }
  }

  return program;
}

int main(int argc, char *argv[]) {
  gflags::SetUsageMessage("Bistra compiler driver.");
  gflags::SetVersionString("0.0.1");
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc != 2) {
    std::cout << "Usage: bistrac [...] program.m\n";
    std::cout << "See --help for more details.\n";
    return 0;
  }
  std::string inFile = argv[1];

  gflags::ShutDownCommandLineFlags();

  // Get the backend.
  auto backend = getBackend(FLAGS_backend);
  assert(backend.get() && "Invalid backend");

  Program *program;
  auto content = readFile(inFile);
  ParserContext ctx(content.c_str(), inFile);

  if (endsWith(inFile, ".bc")) {
    program = Bytecode::deserialize(content);
  } else {
    program = parseProgram(ctx);
  }

  // Unable to parse or load bytecode.
  if (!program)
    return 1;

  if (FLAGS_tune) {
    std::string outFile = "/tmp/file.s";
    if (FLAGS_out.size()) {
      outFile = FLAGS_out;
    } else {
      std::cout << "Output flag (--out) is not set. Using the default: "
                << outFile << "\n";
    }

    optimizeEvaluate(*backend.get(), program, outFile);
  }

  if (FLAGS_opt) {
    ::simplify(program);
    ::promoteLICM(program);
  }

  if (FLAGS_dump) {
    program->dump();
  }

  if (FLAGS_time) {
    auto res = backend->evaluateCode(program, 10);
    std::cout << "The program \"" << program->getName() << "\" completed in "
              << res << " seconds. \n";
  }

  if (FLAGS_warn) {
    analyzeProgram(program, ctx);
  }

  if (FLAGS_out.size()) {
    // Emit bytecode.
    if (FLAGS_bytecode) {
      writeFile(FLAGS_out, Bytecode::serialize(program));
    } else {
      // Emit target-specific object file, or a textual representation
      backend->emitProgramCode(program, FLAGS_out, FLAGS_textual, 10);
    }
  }

  return 0;
}
