#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Transforms.h"

#define STRIP_FLAG_HELP 0
#include "gflags/gflags.h"

#include <fstream>
#include <iostream>

using namespace bistra;

DEFINE_bool(tune, false, "Executes and auto-tune the program.");
DEFINE_bool(time, false, "Executes and times the program.");
DEFINE_string(out, "", "Output destination file to save the compiled program.");


void tune(Program *p, const std::string &outName) {
  auto *p0 = new EvaluatorPass(outName);
  auto *p1 = new PromoterPass(p0);
  auto *p2 = new WidnerPass(p1);
  auto *p3 = new WidnerPass(p2);
  auto *p4 = new VectorizerPass(p3);
  auto *p5 = new TilerPass(p4);
  auto *p6 = new TilerPass(p5);
  p6->doIt(p);
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
  Program *p = parseProgram(content.c_str());

  if (FLAGS_time) {
    ::simplify(p);
    ::promoteLICM(p);
    auto CB = getBackend("C");
    auto res = CB->evaluateCode(p, 10);
    std::cout << "The program \"" << p->getName() << "\" completed in " << res
              << " seconds. \n";
  }


  if (FLAGS_tune) {

    std::string outFile = "/tmp/file.cc";
    if (FLAGS_out.size()) {
      outFile = FLAGS_out;
    } else {
      std::cout << "Output flag (--out) is not set. Using the default: " << outFile << "\n";
    }

    tune(p, outFile);
  }

  return 0;
}
