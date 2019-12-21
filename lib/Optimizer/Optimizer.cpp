#include "bistra/Optimizer/Optimizer.h"
#include "bistra/Analysis/Program.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/Backends.h"
#include "bistra/Bytecode/Bytecode.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"
#include "bistra/Transforms/Transforms.h"

#include <array>
#include <iostream>
#include <set>

using namespace bistra;

class Pass {
  std::string name_;

protected:
  Pass *nextPass_;

public:
  Pass(const std::string &name, Pass *next) : name_(name), nextPass_(next) {}
  virtual void doIt(Program *p) = 0;
};

class EvaluatorPass : public Pass {
  double bestTime_{1000};
  StmtHandle bestProgram_;
  Backend &backend_;
  /// Save the best C program to this optional path, if not empty.
  std::string savePath_;
  /// Is the format textual?
  bool isText_;
  // Is the saved format bytecode?
  bool isBytecode_;
  // A set of already-ran programs hash codes.
  std::set<uint64_t> alreadyRan_;

public:
  EvaluatorPass(Backend &backend, const std::string &savePath, bool isText,
                bool isBytecode)
      : Pass("evaluator", nullptr), bestProgram_(nullptr, nullptr),
        backend_(backend), savePath_(savePath), isText_(isText),
        isBytecode_(isBytecode) {}
  virtual void doIt(Program *p) override;
  Program *getBestProgram() { return (Program *)bestProgram_.get(); }
};

class FilterPass : public Pass {
  Backend &backend_;

public:
  FilterPass(Backend &backend, Pass *next)
      : Pass("filter", next), backend_(backend) {}
  virtual void doIt(Program *p) override;
};

class VectorizerPass : public Pass {
  Backend &backend_;

public:
  VectorizerPass(Backend &backend, Pass *next)
      : Pass("vectorizer", next), backend_(backend) {}
  virtual void doIt(Program *p) override;
};

class InterchangerPass : public Pass {
public:
  InterchangerPass(Pass *next) : Pass("interchange", next) {}
  virtual void doIt(Program *p) override;
};

class TilerPass : public Pass {
public:
  TilerPass(Pass *next) : Pass("tiler", next) {}
  virtual void doIt(Program *p) override;
};

class WidnerPass : public Pass {
  Backend &backend_;

public:
  WidnerPass(Backend &backend, Pass *next)
      : Pass("widner", next), backend_(backend) {}
  virtual void doIt(Program *p) override;
};

class PromoterPass : public Pass {
public:
  PromoterPass(Pass *next) : Pass("promoter", next) {}
  virtual void doIt(Program *p) override;
};

class DistributePass : public Pass {
public:
  DistributePass(Pass *next) : Pass("distribute", next) {}
  virtual void doIt(Program *p) override;
};

void EvaluatorPass::doIt(Program *p) {
  // Check if we already benchmarked this program.
  if (!alreadyRan_.insert(p->hash()).second) {
    std::cout << ":" << std::flush;
    return;
  }

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
      if (isBytecode_) {
        writeFile(savePath_, Bytecode::serialize(p));
      } else {
        // Emit the program code.
        backend_.emitProgramCode(p, savePath_, isText_, 10);
      }
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

/// Collect the arguments that are used in the region \p s.
std::set<Argument*> collectArgsUsed(Stmt *s) {
  std::set<Argument*> args;
  // Scan the first loop and look for buffers.
  for (auto &e : collectExprs(s)) {
    if (auto *gep = dynamic_cast<GEPExpr*>(e)) {
      args.insert(gep->getDest());
    }
  }
  return args;
}

// Try to fuse all of the shallow fusable loops.
bool tryToFuseAllShallowLoops(Program *p) {
  bool changed = false;

restart:
  for (auto *L : collectLoops(p)) {
    // Find the following consecutive loop.
    Loop *L2 = dynamic_cast<Loop*>(getNextStmt(L));

    // We were not able to find a consecutive loop.
    if (!L2)
      return false;

    /// Scan the first and second loops and collect the buffers that we access.
    std::set<Argument*> buffers1 = collectArgsUsed(L);
    std::set<Argument*> buffers2 = collectArgsUsed(L2);

    // The number of shared buffers between L1 and L2.
    unsigned numShared = 0;
    for (auto &A : buffers1) {
      if (buffers2.count(A)) { numShared++; }
    }

    // Heuristics: loops must share most of the buffers.
    unsigned numBuffers = std::max(buffers1.size(), buffers2.size());
    if (numShared < numBuffers/2)
        continue;

    // Okay, the loops share most buffers. Let's merge them.
    bool f = (bool)::fuse(L, 8);
    changed |= f;

    // The fuser deleted a loop. Simplify the code and run again.
    if (f) {
      ::simplify(p);
      goto restart;
    }
  }
  return changed;
}

// Try to vectorize all of the loops.
bool tryToVectorizeAllLoops(Program *p, unsigned VF) {
  bool changed = false;
  for (auto *l : collectLoops(p)) {
    changed |= (bool)::vectorize(l, VF);
  }
  return changed;
}

void VectorizerPass::doIt(Program *p) {
  p->verify();

  // Vectorization Factor:
  unsigned VF = backend_.getRegisterWidth();

  CloneCtx map;
  std::unique_ptr<Program> np((Program *)p->clone(map));

  // The vectorizer pass is pretty simple. Just try to vectorize all loops.
  bool changed = tryToVectorizeAllLoops(np.get(), VF);

  // Try the vectorized version:
  if (changed)
    nextPass_->doIt(np.get());

  // Try the unvectorized code.
  nextPass_->doIt(p);
}

/// Add element \p elem into the ordered set vector \p set.
template <class T> void addOnce(std::vector<T *> &set, T *elem) {
  if (std::find(set.begin(), set.end(), elem) == set.end()) {
    set.push_back(elem);
  }
}

/// \returns a single index that is used as the last dimension for all array
/// access. All access patterns are consecutive on this dimension.
Loop* collectLastIndexForAllIndices(Scope *s) {
  std::vector<Loop *> lastSubscriptIndex;

  std::vector<LoadExpr *> loads;
  std::vector<StoreStmt *> stores;
  collectLoadStores(s, loads, stores);

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
    return nullptr;

  return lastSubscriptIndex.back();
}

// Try to sink loops to allow consecutive access and vectorization.
bool sinkLoopsForConsecutiveIndexAccess(Program *p) {
  bool changed = false;
  for (auto *l : collectInnermostLoops(p)) {
    auto *loopToSink = collectLastIndexForAllIndices(l);
    if (!loopToSink) continue;

    changed |= ::sink(loopToSink, 8);
    p->verify();
  }

  return changed;
}

void InterchangerPass::doIt(Program *p) {
  p->verify();
  CloneCtx map;
  std::unique_ptr<Program> np((Program *)p->clone(map));

  // Sink loops to allow vectorization.
  bool changed = sinkLoopsForConsecutiveIndexAccess(np.get());

  if (changed) {
    nextPass_->doIt(np.get());
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

bool tryToTileForLocality(Program *p) {
  bool changed = false;
  // Collect the innermost loops.
  std::vector<Loop *> innermost = collectInnermostLoops(p);
  unsigned tileSize = 32;

   for (auto *inner : innermost) {
     // Collect the loop nest that contain the current loop.
     Loop *top = getContainingLoop(inner);

     if (!top) continue;

     // Ignore loops that don't touch much memory.
     auto IOPL = getNumLoadsInLoop(top);
     if (IOPL < (1 << 13))
       continue;

     // Don't touch loops that operate on a small tile.
     if (top->getEnd() < tileSize || inner->getEnd() < tileSize)
       continue;

     // All of the loops are consecutive on some dimension. Tiling may not help
     // here.
     auto *lastIndexLoop = collectLastIndexForAllIndices(top);
     if (lastIndexLoop) continue;

     bool t1 = ::tile(inner, tileSize);
     bool t2 = ::tile(top, tileSize);
     // If we were not able to tile the loops just continue and hope we did not
     // mess things up.
     if (!t1 && !t2)
       continue;

     ::hoist(inner, 1);
     changed = true;
   }

  return changed;
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
  std::array<int, 4> widths = {2, 3, 4, 5};
  unsigned numWidths = widths.size();
  p->verify();
  unsigned maxRegs = backend_.getNumRegisters();

  // For each innermost loop:
  for (auto *inner : collectInnermostLoops(p)) {
    // Collect the loop nest that contain the current loop.
    std::vector<Loop *> hierarchy = collectLoopHierarchy(inner, 4);

    // Remove the innermost loop. We don't want to widen it because it is just
    // like unrolling.
    hierarchy.erase(hierarchy.begin());

    // Don't widen zero loops.
    if (hierarchy.size() < 1)
      continue;

    Loop *top = hierarchy.back();

    // Don't touch loops that have zero compute (just write memory).
    if (getComputeIOInfo(top).second == 0)
      continue;

    // Calculate how many different combinations of widths to try. This number
    // encodes all possible combinations. One way to view this is where each
    // tile size is a letter in the alphabet and we iterate over the words and
    // extract one letter at a time.
    unsigned numTries = ipow(numWidths, hierarchy.size());
    bool changed = false;
    assert(numTries < 1e6 && "Too many combinations!");

    // Try all possible block size combinations (see comment above).
    for (int attemptID = 0; attemptID < numTries; attemptID++) {
      CloneCtx map;
      std::unique_ptr<Program> np((Program *)p->clone(map));
      unsigned numRegs = 1;

      int ctr = attemptID;
      for (auto *l : hierarchy) {
        // Pick a width:
        int ws = widths[ctr % numWidths];
        ctr = ctr / numWidths;

        auto *newL = map.get(l);
        changed |= (bool)::widen(newL, ws);
        numRegs *= ws;
      } // Loop hierarchy.

      // Try this configuration.
      if (changed && numRegs <= maxRegs) {
        nextPass_->doIt(np.get());
      }
    } // Tiling attempt.
  }   // Each innermost loop.

  // Try unwidened loops.
  nextPass_->doIt(p);
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
  // Distribute all of the loops to ensure that all of the non-scope stmts are
  // located in innermost loops. This allows us to interchange loops.
  ::distributeAllLoops(np.get());
  ::simplify(np.get());
  nextPass_->doIt(np.get());
}

Program *bistra::optimizeEvaluate(Backend &backend, Program *p,
                                  const std::string &filename, bool isTextual,
                                  bool isBytecode) {

  // A simple search procedure, similar to the one implemented here is
  // described in the paper:
  //
  // Autotuning GEMM Kernels for the Fermi GPU, 2012
  // Kurzak, Jakub and Tomov, Stanimire and Dongarra, Jack

  auto *ev = new EvaluatorPass(backend, filename, isTextual, isBytecode);
  Pass *ps = new FilterPass(backend, ev);
  ps = new PromoterPass(ps);
  ps = new WidnerPass(backend, ps);
  ps = new DistributePass(ps);
  ps = new VectorizerPass(backend, ps);
  ps = new TilerPass(ps);
  ps = new InterchangerPass(ps);
  ps = new DistributePass(ps);
  ps->doIt(p);
  return ev->getBestProgram();
}

std::unique_ptr<Program> bistra::optimizeStatic(Backend *backend, Program *p) {
  bool changed = false;
  CloneCtx map;
  std::unique_ptr<Program> np((Program *)p->clone(map));

  // Vectorization factor.
  unsigned VF = backend->getRegisterWidth();

  // Distribute all of the loops to ensure that all of the non-scope stmts are
  // located in innermost loops. This allows us to interchange loops.
  changed |= ::distributeAllLoops(np.get());
  changed |= ::simplify(np.get());

  // Sink loops to allow consecutive access.
  changed |= sinkLoopsForConsecutiveIndexAccess(np.get());

  // Try to fuse shallow loops.
  changed |= tryToFuseAllShallowLoops(np.get());

  changed |= tryToVectorizeAllLoops(np.get(), VF);

  changed |=  tryToTileForLocality(np.get());

  // Perform LICM and cleanup the program one last time.
  changed |= ::simplify(np.get());
  changed |= ::promoteLICM(np.get());
  changed |= ::simplify(np.get());

  return np;
}
