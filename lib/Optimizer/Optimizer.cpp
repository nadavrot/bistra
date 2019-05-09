#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
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
  }
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

  int tileSizes[] = {16, 32, 56, 64, 128};

  std::vector<Loop *> loops;
  collectLoops(p, loops);

  for (auto *l : loops) {
    for (int ts : tileSizes) {
      CloneCtx map;
      std::unique_ptr<Program> np((Program *)p->clone(map));
      auto *newL = map.get(l);
      if (!::tile(newL, ts))
        continue;

      for (int i = 0; i < 2; i++) {
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
  int widths[] = {2, 3, 4, 5, 6};

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
  nextPass_->doIt(p);

  std::vector<Loop *> loops;
  collectLoops(p, loops);

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
  auto *p4 = new TilerPass(p3);
  auto *p5 = new TilerPass(p4);
  auto *p6 = new VectorizerPass(p5);
  p6->doIt(p);

  return p0->getBestProgram();
}
