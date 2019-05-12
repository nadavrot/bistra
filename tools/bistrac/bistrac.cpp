#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Transforms.h"

#include <fstream>
#include <iostream>

using namespace bistra;

bool timeFlag = false;
bool tuneFlag = false;
bool optFlag = false;
std::string outFile;
std::string inFile;

void parseFlags(int argc, char *argv[]) {
  std::vector<std::string> params;
  for (int i = 1; i < argc; i++) {
    params.push_back(std::string(argv[i]));
  }

  for (int i = 0; i < params.size(); i++) {
    auto op = params[i];
    if (op == "-T" || op == "--time") {
      timeFlag = true;
      continue;
    }

    if (op == "-t" || op == "--tune") {
      tuneFlag = true;
      continue;
    }
    if (op == "-O3" || op == "--opt") {
      optFlag = true;
      continue;
    }
    if (op == "-o" || op == "--out") {
      if (i == (argc - 1)) {
        std::cout << "-o missing an argument\n";
        abort();
      }
      outFile = params[i + 1];
      i++;
      continue;
    }
    inFile = op;
  }
}

void tune(Program *p) {
  auto *p0 = new EvaluatorPass(outFile);
  auto *p1 = new PromoterPass(p0);
  auto *p2 = new WidnerPass(p1);
  auto *p3 = new WidnerPass(p2);
  auto *p4 = new VectorizerPass(p3);
  auto *p5 = new TilerPass(p4);
  auto *p6 = new TilerPass(p5);
  p6->doIt(p);
}

int main(int argc, char *argv[]) {
  parseFlags(argc, argv);

  if (!inFile.size()) {
    std::cout << "Usage: bistrac [--tune|-t] [--opt|-O3] [--out|-o] file.txt\n";
    return 1;
  }

  auto content = readFile(inFile);
  Program *p = parseProgram(content.c_str());

  if (timeFlag) {
    ::simplify(p);
    ::promoteLICM(p);
    auto CB = getBackend("C");
    auto res = CB->evaluateCode(p, 10);
    std::cout << "The program \"" << p->getName() << "\" completed in " << res
              << " seconds. \n";
  }

  if (optFlag) {
    ::simplify(p);
    ::promoteLICM(p);
    auto CB = getBackend("C");
    if (outFile.size()) {
      std::cout << "Outfile unspecified (--out file.cc).\n";
      abort();
    }
    writeFile(outFile, CB->emitBenchmarkCode(p, 10));
  }

  if (tuneFlag) {
    tune(p);
  }

  return 0;
}
