#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"
#include "bistra/Transforms/Transforms.h"

#include <iostream>
#include <set>

using namespace bistra;

void EvaluatorPass::doIt(Program *p) {
  p->verify();
  auto CB = getBackend("C");
  auto res = CB->evaluateCode(p, 10);
  if (res < bestTime_) {
    p->dump();
    std::cout << "New best result: " << res << "\n";
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
  // For each loop.
  for (auto *l : loops) {
    // This loop body is too big.
    if (l->getBody().size() > 64) {
      return;
    }

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

void TilerPass::doIt(Program *p) {
  p->verify();
  nextPass_->doIt(p);

  int tileSizes[] = {32, 64, 128, 256};

  std::vector<Loop *> loops;
  collectLoops(p, loops);

  for (auto *l : loops) {
    // Don't try to tile small loops.
    if (l->getEnd() < 128)
      continue;

    for (int ts : tileSizes) {
      // Round the tile size to match the stride steps.
      unsigned adjustedTileSize = ts - (ts % l->getStride());

      CloneCtx map;
      std::unique_ptr<Program> np((Program *)p->clone(map));
      auto *newL = map.get(l);
      if (!::tile(newL, adjustedTileSize))
        continue;

      for (int i = 0; i < 3; i++) {
        if (::hoist(newL, 1)) {
          nextPass_->doIt(np.get());
        }
      }
    }
  }
}

void WidnerPass::doIt(Program *p) {
  p->verify();
  nextPass_->doIt(p);
  int widths[] = {2, 3, 4};

  std::vector<Loop *> loops;
  collectLoops(p, loops);

  for (auto *l : loops) {
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
  std::unique_ptr<Program> np((Program *)p->clone(map));
  ::simplify(np.get());
  ::promoteLICM(np.get());
  nextPass_->doIt(np.get());
}

Program *bistra::optimizeEvaluate(Program *p) {
  auto *p0 = new EvaluatorPass();
  auto *p1 = new PromoterPass(p0);
  auto *p2 = new WidnerPass(p1);
  auto *p3 = new WidnerPass(p2);
  auto *p4 = new VectorizerPass(p3);
  auto *p5 = new TilerPass(p4);
  auto *p6 = new TilerPass(p5);
  p6->doIt(p);

  return p0->getBestProgram();
}
