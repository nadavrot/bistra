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

  auto res = backend_.evaluateCode(p, 10);
  if (res < bestTime_) {
    p->dump();
    std::cout << "New best result: " << res << ", "
              << prettyPrintNumber(info.second / res) << " flops/sec. \n";

    bestTime_ = res;
    bestProgram_.setReference(p->clone());

    if (savePath_.size()) {
      remove(savePath_.c_str());
      backend_.emitProgramCode(p, savePath_, true, 10);
    }
  } else {
    std::cout << "." << std::flush;
  }
}

/// \returns a list of innermost loops in \p s.
static std::vector<Loop *> collectInnermostLoops(Scope *s) {
  auto loops = collectLoops(s);
  std::vector<Loop *> innermost;
  for (auto *l : loops) {
    if (isInnermostLoop(l))
      innermost.push_back(l);
  }
  return innermost;
}

/// \returns upto \p levels loop nest that wrap the loop \p L.
static std::vector<Loop *> collectLoopHierarchy(Loop *L, int levels) {
  // Collect the loop nest that contain the current loop.
  std::vector<Loop *> hierarchy;
  Loop *lptr = L;
  for (int i = 0; lptr && i < levels; i++) {
    hierarchy.push_back(lptr);
    lptr = getContainingLoop(lptr);
  }

  return hierarchy;
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
    if (local > backend_.getNumRegisters()) {
      return;
    }
  }

  // All of the filters passed. Move on to the next level.
  nextPass_->doIt(p);
}

void VectorizerPass::doIt(Program *p) {
  p->verify();

  // Vectorization Factor:
  unsigned VF = backend_.getRegisterWidth();

  // The vectorizer pass is pretty simple. Just try to vectorize all loops.
  std::vector<Loop *> loops;
  collectLoops(p, loops);
  for (auto *l : loops) {
    CloneCtx map;
    std::unique_ptr<Program> np((Program *)p->clone(map));
    auto *newL = map.get(l);
    if (::vectorize(newL, VF)) {
      nextPass_->doIt(np.get());
    }
  }

  // Try the unvectorized code.
  nextPass_->doIt(p);
}

/// Add element \p elem into the ordered set vector \p set.
template <class T> void addOnce(std::vector<T *> &set, T *elem) {
  if (std::find(set.begin(), set.end(), elem) == set.end()) {
    set.push_back(elem);
  }
}

void InterchangerPass::doIt(Program *p) {
  p->verify();

  for (auto *l : collectInnermostLoops(p)) {
    std::vector<Loop *> lastSubscriptIndex;

    std::vector<LoadExpr *> loads;
    std::vector<StoreStmt *> stores;
    collectLoadStores(l, loads, stores);

    // Collect loops that are used as the last index for some load.
    for (auto *st : stores) {
      if (auto *idx = dynamic_cast<IndexExpr *>(st->getIndices().back().get()))
        addOnce(lastSubscriptIndex, idx->getLoop());
    }
    for (auto *ld : loads) {
      if (auto *idx = dynamic_cast<IndexExpr *>(ld->getIndices().back().get()))
        addOnce(lastSubscriptIndex, idx->getLoop());
    }

    // If there is only one dominent dimension then try to sink it down.
    if (lastSubscriptIndex.size() != 1)
      continue;

    auto loopToSink = lastSubscriptIndex.back();

    CloneCtx map;
    std::unique_ptr<Program> np((Program *)p->clone(map));
    auto *newL = map.get(loopToSink);
    if (::sink(newL, 8)) {
      // If we were not able to sink the loop all the way then quit.
      if (!isInnermostLoop(newL))
        continue;
      np->verify();
      nextPass_->doIt(np.get());
    }
  }

  // Evaluate the original version.
  nextPass_->doIt(p);
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

/// \returns a ** b;
static uint64_t ipow(uint64_t a, uint64_t b) {
  uint64_t res = 1;
  for (int i = 0; i < b; i++) {
    res *= a;
  }
  return res;
}

void TilerPass::doIt(Program *p) {
  std::array<int, 6> tileSize = {8, 16, 32, 64, 128, 256};
  unsigned numTiles = tileSize.size();
  p->verify();
  nextPass_->doIt(p);

  // Collect the innermost loops.
  std::vector<Loop *> innermost = collectInnermostLoops(p);

  for (auto *inner : innermost) {
    // Collect the loop nest that contain the current loop.
    std::vector<Loop *> hierarchy = collectLoopHierarchy(inner, 4);

    // Don't tile a single loop.
    if (hierarchy.size() < 2)
      continue;

    Loop *top = hierarchy.back();

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
    unsigned numTries = ipow(numTiles, hierarchy.size());
    bool changed = false;
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
        changed |= ::hoist(newL, hierarchy.size());
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

  (void)backend_;

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
  auto loops = collectLoops(np.get());
  p->verify();
  for (auto *l : loops) {
    if (splitScopes(l))
      goto restart;
  }
  ::simplify(np.get());
  nextPass_->doIt(np.get());
}

Program *bistra::optimizeEvaluate(Backend &backend, Program *p,
                                  const std::string &filename) {

  // A simple search procedure, similar to the one implemented here is
  // described in the paper:
  //
  // Autotuning GEMM Kernels for the Fermi GPU, 2012
  // Kurzak, Jakub and Tomov, Stanimire and Dongarra, Jack

  auto *ev = new EvaluatorPass(backend, filename);
  Pass *ps = new FilterPass(backend, ev);
  ps = new PromoterPass(ps);
  ps = new WidnerPass(backend, ps);
  ps = new WidnerPass(backend, ps);
  ps = new DistributePass(ps);
  ps = new VectorizerPass(backend, ps);
  ps = new TilerPass(ps);
  ps = new InterchangerPass(ps);
  ps = new DistributePass(ps);
  ps->doIt(p);
  return ev->getBestProgram();
}
