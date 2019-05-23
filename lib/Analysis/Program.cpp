#include "bistra/Analysis/Program.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include <array>
#include <set>

using namespace bistra;

uint64_t bistra::getNumLoadsInLoop(Loop *L) {
  std::vector<LoadExpr *> loads;
  std::vector<StoreStmt *> stores;
  collectLoadStores(L, loads, stores);

  // Insert all of the sub loops into the live set.
  std::vector<Loop *> loops = collectLoops(L);
  std::set<Loop *> live(loops.begin(), loops.end());
  live.insert(L);

  uint64_t mx = 0;

  for (auto *ld : loads) {
    mx += getAccessedMemoryForSubscript(ld->getIndices(), &live);
  }
  return mx;
}

uint64_t bistra::getNumArithmeticInLoop(Loop *L) {
  std::unordered_map<ASTNode *, ComputeCostTy> heatmap;
  estimateCompute(L, heatmap);
  assert(heatmap.count(L) && "No information for the program");
  return heatmap[L].second;
}
