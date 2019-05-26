#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Analysis/Program.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"
#include "bistra/Transforms/Transforms.h"

#include <array>
#include <iostream>
#include <set>

using namespace bistra;

void EvaluatorPass::doIt(Program *p) {
  p->verify();

  std::unordered_map<ASTNode *, ComputeCostTy> heatmap;
  estimateCompute(p, heatmap);
  assert(heatmap.count(p) && "No information for the program");
  auto info = heatmap[p];

  auto CB = getBackend("C");
  auto res = CB->evaluateCode(p, 10);
  if (res < bestTime_) {
    p->dump();
    std::cout << "New best result: " << res << ", "
              << prettyPrintNumber(info.second / res) << " flops/sec. \n";

    bestTime_ = res;
    bestProgram_.setReference(p->clone());

    if (savePath_.size()) {
      remove(savePath_.c_str());
      writeFile(savePath_, CB->emitBenchmarkCode(p, 10));
    }
  } else {
    std::cout << "." << std::flush;
  }
}

void FilterPass::doIt(Program *p) {
  std::vector<Loop *> loops;
  collectLoops(p, loops);

  // Don't allow innerloops with huge bodies or with too many local registers.
  for (auto *l : loops) {
    // This loop body is too big.
    if (l->getBody().size() > 64) {
      return;
    }

    // Count local registers.
    unsigned local = 0;
    for (auto &s : l->getBody()) {
      if (dynamic_cast<StoreLocalStmt *>(s.get())) {
        local++;
      }
    }

    // This loop must spill (on CPUs). Abort.
    if (local > 16) {
      return;
    }
  }

  // All of the filters passed. Move on to the next level.
  nextPass_->doIt(p);
}

void VectorizerPass::doIt(Program *p) {
  p->verify();
  nextPass_->doIt(p);

  // The vectorizer pass is pretty simple. Just try to vectorize all loops.
  std::vector<Loop *> loops;
  collectLoops(p, loops);
  for (auto *l : loops) {
    CloneCtx map;
    std::unique_ptr<Program> np((Program *)p->clone(map));
    auto *newL = map.get(l);
    if (::vectorize(newL, 8)) {
      nextPass_->doIt(np.get());
    }
  }
}

/// Compute the arithmetic and IO properties for the loop \p L.
static ComputeCostTy getComputeIOInfo(Loop *L) {
  std::unordered_map<ASTNode *, ComputeCostTy> heatmap;
  estimateCompute(L, heatmap);
  assert(heatmap.count(L) && "No information for the program");
  return heatmap[L];
}

/// Calcualte a possible tile size that matches the stride.
static unsigned roundTileSize(unsigned tileSize, unsigned stride) {
  return tileSize - (tileSize % stride);
}

void TilerPass::doIt(Program *p) {
  std::array<int, 6> tileSize = {8, 16, 32, 64, 128, 256};
  unsigned numTiles = tileSize.size();
  p->verify();
  nextPass_->doIt(p);

  // Collect the innermost loops.
  auto loops = collectLoops(p);
  std::vector<Loop *> innermost;
  for (auto *l : loops) {
    if (isInnermostLoop(l))
      innermost.push_back(l);
  }

  for (auto *inner : innermost) {
    // Collect the loop nestt that contain the current loop.
    std::vector<Loop *> hierarchy;
    Loop *lptr = inner;
    for (int i = 0; lptr && i < 4; i++) {
      hierarchy.push_back(lptr);
      lptr = getContainingLoop(lptr);
    }

    // Don't tile a single loop.
    if (hierarchy.size() < 2)
      continue;

    Loop *top = hierarchy[hierarchy.size() - 1];

    // Don't touch loops that have zero compute (just write memory).
    if (getComputeIOInfo(top).second == 0)
      continue;

    // Ignore loops that don't touch much memory.
    auto IOPL = getNumLoadsInLoop(top);
    if (IOPL < (1 << 13))
      continue;

    // Calculate how many different combinations of blocks to try. This number
    // encodes all possible combinations. One way to view this is where each
    // tile size is a letter in the alphabet and we iterate over the words and
    // extract one letter at a time.
    unsigned numTries = 1;
    bool changed = false;
    for (int i = 0; i < hierarchy.size(); i++) {
      numTries *= numTiles;
    }
    assert(numTries < 1e6 && "Too many combinations!");

    // Try all possible block size combinations (see comment above).
    for (int attemptID = 0; attemptID < numTries; attemptID++) {

      CloneCtx map;
      std::unique_ptr<Program> np((Program *)p->clone(map));

      int ctr = attemptID;
      for (auto *l : hierarchy) {
        // Pick the last
        int currBlockSize = tileSize[ctr % numTiles];
        ctr = ctr / numTiles;

        // Adjust the tile size to the loop stride.
        auto ts = roundTileSize(currBlockSize, l->getStride());
        if (ts == 0)
          continue;

        auto *newL = map.get(l);
        if (!::tile(newL, ts))
          continue;

        // Hoist the loop twice.
        changed |= ::hoist(newL, 1);
        changed |= ::hoist(newL, 1);
      } // Loop hierarchy.
      if (changed) {
        nextPass_->doIt(np.get());
      }
    } // Tiling attempt.
  }   // Each innermost loop.
}

void WidnerPass::doIt(Program *p) {
  p->verify();
  nextPass_->doIt(p);
  int widths[] = {2, 3, 4};

  std::vector<Loop *> loops;
  collectLoops(p, loops);

  for (auto *l : loops) {
    // Don't touch loops that have zero compute (just write memory).
    if (getComputeIOInfo(l).second == 0)
      continue;

    // Don't try to widen innermost loop because this is exactly like unrolling.
    if (isInnermostLoop(l))
      continue;

    for (int ws : widths) {
      CloneCtx map;
      std::unique_ptr<Program> np((Program *)p->clone(map));
      auto *newL = map.get(l);
      if (!::widen(newL, ws))
        continue;
      nextPass_->doIt(np.get());
    }
  }
}

void PromoterPass::doIt(Program *p) {
  p->verify();
  CloneCtx map;
  // This is a simple cleanup pass.
  std::unique_ptr<Program> np((Program *)p->clone(map));
  ::simplify(np.get());
  ::promoteLICM(np.get());
  nextPass_->doIt(np.get());
}

void DistributePass::doIt(Program *p) {
  p->verify();
  CloneCtx map;
  std::unique_ptr<Program> np((Program *)p->clone(map));

  // Distribute all of tthe loops to ensure that all of the non-scope stmts are
  // located in innermost loops. This allows us to interchange loops.
restart:
  auto loops = collectLoops(p);
  p->verify();
  for (auto *l : loops) {
    if (splitScopes(l))
      goto restart;
  }
  ::simplify(np.get());
  nextPass_->doIt(np.get());
}

Program *bistra::optimizeEvaluate(Program *p, const std::string &filename) {
  auto *ev = new EvaluatorPass(filename);
  Pass *ps = new FilterPass(ev);
  ps = new PromoterPass(ps);
  ps = new WidnerPass(ps);
  ps = new WidnerPass(ps);
  ps = new DistributePass(ps);
  ps = new VectorizerPass(ps);
  ps = new TilerPass(ps);
  ps = new DistributePass(ps);
  ps->doIt(p);
  return ev->getBestProgram();
}
