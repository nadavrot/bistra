#include "bistra/Program/Utils.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Types.h"

#include <fstream>
#include <iostream>
#include <set>
#include <vector>

using namespace bistra;

namespace {
/// A visitor class that collects all loads/stores to locals.
struct LocalsCollector : public NodeVisitor {
  std::vector<LoadLocalExpr *> &loads_;
  std::vector<StoreLocalStmt *> &stores_;
  LocalVar *filter_;
  LocalsCollector(std::vector<LoadLocalExpr *> &loads,
                  std::vector<StoreLocalStmt *> &stores, LocalVar *filter)
      : loads_(loads), stores_(stores), filter_(filter) {}

  virtual void enter(Expr *E) override {
    if (auto *LL = dynamic_cast<LoadLocalExpr *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && LL->getDest() != filter_)
        return;
      loads_.push_back(LL);
    }
  }

  virtual void enter(Stmt *E) override {
    if (auto *SL = dynamic_cast<StoreLocalStmt *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && SL->getDest() != filter_)
        return;
      stores_.push_back(SL);
    }
  }
};
} // namespace

namespace {
/// A visitor class that collects all loads/stores.
struct LoadStoreCollector : public NodeVisitor {
  std::vector<LoadExpr *> &loads_;
  std::vector<StoreStmt *> &stores_;
  Argument *filter_;

  LoadStoreCollector(std::vector<LoadExpr *> &loads,
                     std::vector<StoreStmt *> &stores, Argument *filter)
      : loads_(loads), stores_(stores), filter_(filter) {}

  virtual void enter(Expr *E) override {
    if (auto *LL = dynamic_cast<LoadExpr *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && LL->getDest() != filter_)
        return;
      loads_.push_back(LL);
    }
  }

  virtual void enter(Stmt *E) override {
    if (auto *SL = dynamic_cast<StoreStmt *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && SL->getDest() != filter_)
        return;
      stores_.push_back(SL);
    }
  }
};
} // namespace

namespace {
/// A visitor class that visits all IndexExpr nodes in the program. Uses
/// optional filter to collect only indices for one specific loop.
struct IndexCollector : public NodeVisitor {
  std::vector<IndexExpr *> &indices_;
  Loop *filter_;
  IndexCollector(std::vector<IndexExpr *> &indices, Loop *filter)
      : indices_(indices), filter_(filter) {}
  virtual void enter(Expr *E) override {
    if (IndexExpr *IE = dynamic_cast<IndexExpr *>(E)) {
      // Apply the optional filter and ignore loops that are not the requested
      // loop.
      if (filter_ && IE->getLoop() != filter_)
        return;
      indices_.push_back(IE);
    }
  }
};
} // namespace

namespace {
/// A visitor class that visits all loops in the program.
struct LoopCollector : public NodeVisitor {
  std::vector<Loop *> &loops_;
  LoopCollector(std::vector<Loop *> &loops) : loops_(loops) {}
  virtual void enter(Stmt *E) override {
    if (Loop *L = dynamic_cast<Loop *>(E)) {
      loops_.push_back(L);
    }
  }
};

/// A visitor class that visits all ifs in the program.
struct IfCollector : public NodeVisitor {
  std::vector<IfRange *> &ifs_;
  IfCollector(std::vector<IfRange *> &ifs) : ifs_(ifs) {}
  virtual void enter(Stmt *E) override {
    if (auto *I = dynamic_cast<IfRange *>(E)) {
      ifs_.push_back(I);
    }
  }
};
} // namespace

void bistra::collectLocals(ASTNode *S, std::vector<LoadLocalExpr *> &loads,
                           std::vector<StoreLocalStmt *> &stores,
                           LocalVar *filter) {
  LocalsCollector IC(loads, stores, filter);
  S->visit(&IC);
}

/// Collect all of the load/store accesses to arguments.
/// If \p filter is set then only accesses to \p filter are collected.
void bistra::collectLoadStores(ASTNode *S, std::vector<LoadExpr *> &loads,
                               std::vector<StoreStmt *> &stores,
                               Argument *filter) {
  LoadStoreCollector IC(loads, stores, filter);
  S->visit(&IC);
}

void bistra::collectIndices(ASTNode *S, std::vector<IndexExpr *> &indices,
                            Loop *filter) {
  IndexCollector IC(indices, filter);
  S->visit(&IC);
}

bool bistra::dependsOnLoop(ASTNode *N, Loop *L) {
  std::vector<IndexExpr *> indices;
  collectIndices(N, indices, L);
  return indices.size();
}

void bistra::collectLoops(Stmt *S, std::vector<Loop *> &loops) {
  LoopCollector IC(loops);
  S->visit(&IC);
}

void bistra::collectIfs(Stmt *S, std::vector<IfRange *> &ifs) {
  IfCollector IC(ifs);
  S->visit(&IC);
}

Loop *bistra::getLoopByName(Stmt *S, const std::string &name) {
  std::vector<Loop *> loops;
  collectLoops(S, loops);
  for (auto *L : loops) {
    if (L->getName() == name) {
      return L;
    }
  }
  return nullptr;
}

/// Collect all of the load/store accesses to locals.
/// If \p filter is set then only accesses to \p filter are collected.
void bistra::collectLocals(ASTNode *S, std::vector<LoadLocalExpr *> &loads,
                           std::vector<StoreLocalStmt *> &stores,
                           LocalVar *filter);

Expr *bistra::getZeroExpr(ExprType &T) {
  Expr *ret;
  // Zero scalar:
  if (T.isIndexTy()) {
    ret = new ConstantExpr(0);
  } else {
    ret = new ConstantFPExpr(0.0);
  }

  // Widen if we are requested a vector.
  if (T.isVector()) {
    ret = new BroadcastExpr(ret, T.getWidth());
  }

  assert(ret->getType() == T);
  return ret;
}

bool bistra::areLoadsStoresDisjoint(const std::vector<LoadExpr *> &loads,
                                    const std::vector<StoreStmt *> &stores) {
  std::set<Argument *> writes;
  // Collect the write destination.
  for (auto *st : stores) {
    writes.insert(st->getDest());
  }
  for (auto *ld : loads) {
    // A pair of load/store wrote into the same buffer. Aborting.
    if (writes.count(ld->getDest())) {
      return false;
    }
  }
  return true;
}

namespace {
/// Calculates the roofline model for the program.
struct ComputeEstimator : public NodeVisitor {
  std::unordered_map<ASTNode *, ComputeCostTy> &heatmap_;

  ComputeEstimator(std::unordered_map<ASTNode *, ComputeCostTy> &heatmap)
      : heatmap_(heatmap) {}

  virtual void leave(Expr *E) override {
    // Loads count as one memory op and zero compute.
    if (auto *LE = dynamic_cast<LoadExpr *>(E)) {
      heatmap_[LE] = {1, 0};
      return;
    }
    // Load locals count as zeo memory op and zero compute.
    if (auto *LL = dynamic_cast<LoadLocalExpr *>(E)) {
      heatmap_[LL] = {0, 0};
      return;
    }
    // Binary ops add one arithmetic cost to the cost of both sides.
    if (auto *BE = dynamic_cast<BinaryExpr *>(E)) {
      assert(heatmap_.count(BE->getLHS()));
      assert(heatmap_.count(BE->getRHS()));
      auto LHS = heatmap_[BE->getLHS()];
      auto RHS = heatmap_[BE->getRHS()];
      // Don't count loop and index arithmetic as arithmetic.
      int cost = BE->getLHS()->getType().isIndexTy() ? 0 : 1;
      heatmap_[BE] = {LHS.first + RHS.first, cost + LHS.second + RHS.second};
      return;
    }
    // Broadcast counts as one arithmetic op.
    if (auto *BE = dynamic_cast<BroadcastExpr *>(E)) {
      assert(heatmap_.count(BE->getValue()));
      auto V = heatmap_[BE->getValue()];
      heatmap_[BE] = {V.first, 1 + V.second};
      return;
    }
    // Constants and indices have no cost.
    if (dynamic_cast<IndexExpr *>(E) || dynamic_cast<ConstantExpr *>(E) ||
        dynamic_cast<ConstantFPExpr *>(E)) {
      heatmap_[E] = {0, 0};
      return;
    }
    assert(false && "Unknown expression");
  }

  virtual void leave(Stmt *E) override {
    // Loop expressions multipliy the cost of the sum of the body cost.
    if (auto *LE = dynamic_cast<Loop *>(E)) {
      ComputeCostTy total = {0, 0};
      auto tripcount = LE->getEnd() / LE->getStride();
      // Add the cost of all sub-expressions.
      for (auto &s : LE->getBody()) {
        assert(heatmap_.count(s.get()));
        auto res = heatmap_[s.get()];
        total.first += res.first * tripcount;
        total.second += res.second * tripcount;
      }
      heatmap_[LE] = total;
      return;
    }

    // If expressions accumulate the cost of sub-stmt and add the cost of the
    // if-check. We assume 100% success rate.
    if (auto *IR = dynamic_cast<IfRange *>(E)) {
      auto idx = IR->getIndex().get();
      assert(heatmap_.count(idx));
      ComputeCostTy total = heatmap_[idx];

      // Add the cost of all sub-expressions.
      for (auto &s : IR->getBody()) {
        assert(heatmap_.count(s.get()));
        auto res = heatmap_[s.get()];
        total.first += res.first;
        total.second += res.second;
      }
      heatmap_[IR] = total;
      return;
    }

    // Programs accumulate the cost of sub-stmts.
    if (auto *P = dynamic_cast<Program *>(E)) {
      ComputeCostTy total = {0, 0};
      // Add the cost of all sub-expressions.
      for (auto &s : P->getBody()) {
        assert(heatmap_.count(s.get()));
        auto res = heatmap_[s.get()];
        total.first += res.first;
        total.second += res.second;
      }
      heatmap_[P] = total;
      return;
    }

    // Stores are considered as one memory op, plus the cost of the value.
    if (auto *SS = dynamic_cast<StoreStmt *>(E)) {
      auto val = SS->getValue().get();
      assert(heatmap_.count(val));
      ComputeCostTy total = heatmap_[val];
      total.first += 1;
      if (SS->isAccumulate()) {
        // Accumulate is load+add+store.
        total.first += 2;
        total.second += 1;
      } else {
        total.first += 1;
      }
      heatmap_[SS] = total;
      return;
    }

    // Stores to locals are considered as zero memory ops.
    if (auto *SL = dynamic_cast<StoreLocalStmt *>(E)) {
      auto val = SL->getValue().get();
      assert(heatmap_.count(val));
      heatmap_[SL] = heatmap_[val];
      return;
    }

    assert(false && "Unknown statement");
  }
};
} // namespace

void bistra::estimateCompute(
    Stmt *S, std::unordered_map<ASTNode *, ComputeCostTy> &heatmap) {
  ComputeEstimator CE(heatmap);
  S->visit(&CE);
}

void bistra::dumpProgramFrequencies(Scope *P) {
  std::unordered_map<ASTNode *, ComputeCostTy> heatmap;
  estimateCompute(P, heatmap);

  std::vector<Loop *> loops;
  collectLoops(P, loops);

  assert(heatmap.count(P) && "No information for the program");
  auto info = heatmap[P];
  std::cout << "Total cost:\n"
            << "\tmem ops: " << info.first << "\n\tarith ops: " << info.second
            << "\n";

  for (auto *L : loops) {
    assert(heatmap.count(L) && "No information for the loop");
    auto info = heatmap[L];
    std::cout << "\tLoop " << L->getName() << " stride: " << L->getStride()
              << " body: " << L->getBody().size() << " mem ops: " << info.first
              << " arith ops: " << info.second << "\n";
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
