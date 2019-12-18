#include "bistra/Program/Utils.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Types.h"

#include <fstream>
#include <iostream>
#include <set>
#include <vector>

using namespace bistra;

void bistra::dumpProgramFrequencies(Scope *P) {
  std::unordered_map<ASTNode *, ComputeCostTy> heatmap;
  estimateCompute(P, heatmap);

  std::vector<Loop *> loops;
  collectLoops(P, loops);

  assert(heatmap.count(P) && "No information for the program");
  auto info = heatmap[P];
  std::cout << "Total cost:\n"
            << "\tmem ops: " << prettyPrintNumber(info.first)
            << "\n\tarith ops: " << prettyPrintNumber(info.second) << "\n";

  for (auto *L : loops) {
    assert(heatmap.count(L) && "No information for the loop");
    auto info = heatmap[L];
    std::cout << "\tLoop " << L->getName() << " stride: " << L->getStride()
              << " body: " << L->getBody().size()
              << " mem ops: " << prettyPrintNumber(info.first)
              << " arith ops: " << prettyPrintNumber(info.second) << "\n";
  }
}

void bistra::writeFile(const std::string &filename,
                       const std::string &content) {
  remove(filename.c_str());
  std::ofstream out(filename);
  if (!out.good()) {
    std::cout << "Unable to save the file " << filename << "\n";
    abort();
  }
  out << content;
  out.close();
  return;
}

std::string bistra::readFile(const std::string &filename) {
  std::string result;
  std::string line;
  std::ifstream myfile(filename);
  if (!myfile.good()) {
    std::cout << "Unable to open the file " << filename << "\n";
    abort();
  }
  while (std::getline(myfile, line)) {
    result += line + "\n";
  }
  return result;
}

std::string bistra::prettyPrintNumber(uint64_t num) {

  const char *units[] = {"", "K", "M", "G", "T", "P", "E"};

  int unit = 0;
  while (num > 1000) {
    num = num / 1000;
    unit++;
  }

  return std::to_string(num) + units[unit];
}

uint64_t bistra::ror(uint64_t x, unsigned int bits) {
  return (x >> bits) | (x << (64 - bits));
}

uint64_t bistra::hashJoin(uint64_t one, uint64_t two) {
  return ror(one, 8) ^ ror(two, 16) ^ (one * two);
}

uint64_t bistra::hashJoin(uint64_t one, uint64_t two, uint64_t three) {
  return hashJoin(one, hashJoin(two, three));
}

uint64_t bistra::hashString(const std::string &str) {
  uint64_t h = 0;
  for (char c : str) {
    h = hashJoin(h, c);
  }
  return h;
}
