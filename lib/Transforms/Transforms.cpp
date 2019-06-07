#include "bistra/Transforms/Transforms.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Program/Pragma.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"
#include "bistra/Transforms/Simplify.h"

#include <set>

using namespace bistra;

/// Generate a new index name based on the original name \p origName, the
/// suffix \p suffix and a running counter \p index.
static std::string newIndexName(const std::string &origName,
                                const std::string &suffix, unsigned index) {
  return origName + "_" + suffix + "_" + std::to_string(index);
}

Loop *bistra::tile(Loop *L, unsigned blockSize) {
  // No need to tile if the whole loop fits in one tile.
  if (L->getEnd() <= blockSize)
    return nullptr;

  // Block size must be multiple of the stride (loop processing width).
  if (blockSize % L->getStride())
    return nullptr;

  // We need the range check on the last block only if the block size does not
  // divide the range perfectly.
  bool needRangeCheck = L->getEnd() % blockSize;

  auto origLoopRange = L->getEnd();

  // Create a new loop.
  Loop *NL = new Loop(newIndexName(L->getName(), "tile", blockSize),
                      L->getLoc(), blockSize, L->getStride());

  // Update the original-loop's trip count.
  L->setEnd(L->getEnd() / blockSize + (needRangeCheck ? 1 : 0));
  L->setStride(1);

  // Insert the new loop by moving the content of the original loop. Insert a
  // range check if needed.
  if (needRangeCheck) {
    Scope *IR = new IfRange(new IndexExpr(L), 0, origLoopRange, L->getLoc());
    IR->takeContent(L);
    NL->addStmt(IR);
  } else {
    NL->takeContent(L);
  }
  L->addStmt(NL);

  // Update all of the indices in the program to refer to the combination of
  // two indices of the two loops.
  std::vector<IndexExpr *> indices;
  collectIndices(L, indices, L);

  for (auto *idx : indices) {
    // I -> (I * bs) + I.tile;
    auto *mul = new BinaryExpr(new IndexExpr(L), new ConstantExpr(blockSize),
                               BinaryExpr::BinOpKind::Mul, L->getLoc());
    auto *expr = new BinaryExpr(new IndexExpr(NL), mul,
                                BinaryExpr::BinOpKind::Add, L->getLoc());
    idx->replaceUseWith(expr);
  }

  return NL;
}

bool bistra::split(Loop *L) {
  // Check if there is anything to split.
  if (L->getBody().size() < 2)
    return false;

  unsigned cnt = 0;
  // For each statement in the original loop.
  for (auto &S : L->getBody()) {
    Loop *NL = new Loop(newIndexName(L->getName(), "split", cnt++), L->getLoc(),
                        L->getEnd(), L->getStride());

    // Copy the content.
    CloneCtx map;
    NL->addStmt(S->clone(map));

    // Replace the indices of the old loop with the new looop.
    std::vector<IndexExpr *> indices;
    collectIndices(NL, indices, L);
    for (auto *IE : indices) {
      IE->replaceUseWith(new IndexExpr(NL));
    }

    // Insert before to preserve the original stmt order.
    ((Scope *)L->getParent())->insertBeforeStmt(NL, L);
  }

  // Remove the original loop.
  ((Scope *)L->getParent())->removeStmt(L);
  return true;
}

bool bistra::splitScopes(Loop *L) {
  L->verify();
  // Divide the statements in the original loop into packets of statements that
  // go together. Scopes must reside in their own packet.
  std::vector<std::vector<Stmt *>> packets;
  bool lastIterWasScope = true;
  for (auto &S : L->getBody()) {
    if (isScope(S)) {
      // Push a new packet.
      packets.push_back({S});
      lastIterWasScope = true;
      continue;
    }

    // Non-scope stmts.
    if (lastIterWasScope) {
      lastIterWasScope = false;
      // Open a new packet.
      packets.push_back({S});
    } else {
      // Add to existing packet.
      packets.back().push_back(S);
    }
  }

  // Check if there is anything to split.
  if (packets.size() < 2)
    return false;

  unsigned cnt = 0;
  // For each packet of statement in the original loop.
  for (auto &packet : packets) {
    // Start a loop for this packet.
    Loop *NL = new Loop(newIndexName(L->getName(), "split", cnt++), L->getLoc(),
                        L->getEnd(), L->getStride());

    // Copy the content.
    CloneCtx map;
    for (Stmt *ss : packet) {
      NL->addStmt(ss->clone(map));
    }

    // Replace the indices of the old loop with the new looop.
    std::vector<IndexExpr *> indices;
    collectIndices(NL, indices, L);
    for (auto *IE : indices) {
      IE->replaceUseWith(new IndexExpr(NL));
    }

    // Insert before to preserve the original stmt order.
    ((Scope *)L->getParent())->insertBeforeStmt(NL, L);
  }

  // Remove the original loop.
  ((Scope *)L->getParent())->removeStmt(L);
  return true;
}

bool bistra::hoist(Loop *L, unsigned levels) {
  if (levels == 0)
    return false;

  Scope *parent = dynamic_cast<Scope *>(L->getParent());
  if (!parent)
    return false;

  // Can't hoist because the parent loop has other code in the body.
  if (parent->getBody().size() != 1)
    return false;

  // Check if we have a parent.
  auto *PH = parent->getOwnerHandle();
  if (!PH)
    return false;

  // Swap this loop and the one above it.
  parent->clear();
  parent->takeContent(L);
  L->addStmt(parent);
  PH->setReference(L);

  // Try to hoist the loop again.
  hoist(L, levels - 1);
  return true;
}

bool bistra::unrollLoop(Loop *L, unsigned maxTripCount) {
  Scope *parent = dynamic_cast<Scope *>(L->getParent());
  assert(parent && "Unexpected parent shape");

  if (L->getEnd() > maxTripCount)
    return false;

  std::vector<Stmt *> unrolledBodies;

  // For each unroll iteration:
  for (unsigned iter = 0; iter < L->getEnd(); iter++) {
    // For each statement in the body of the loop:
    for (auto &ST : L->getBody()) {
      // Copy the body.
      CloneCtx map;
      auto *newSt = ST->clone(map);
      // Collect the indices.
      std::vector<IndexExpr *> indices;
      collectIndices(newSt, indices, L);
      // Update the indices to the constant iter number.
      for (auto *IE : indices) {
        IE->replaceUseWith(new ConstantExpr(iter));
      }
      // Save the unrolled loop body and paste it later instead of the original
      // loop.
      unrolledBodies.push_back(newSt);
    }
  }

  // Insert the new loops before the original loop.
  for (auto *newLoop : unrolledBodies) {
    parent->insertBeforeStmt(newLoop, L);
  }
  parent->removeStmt(L);
  return true;
}

Loop *bistra::peelLoop(Loop *L, int k) {
  unsigned origLoopEndRange = L->getEnd();
  // If K is a negative number then peel from the end of the loop.
  if (k < 1) {
    // Check if the k fits within the loop range:
    if ((-k) < origLoopEndRange) {
      // Adding K because it is negative.
      k = origLoopEndRange + k;
    } else {
      return nullptr;
    }
  }

  // Trip count must be smaller than the partition size, and the peeled portion
  // must be a multiple of the loop stride.
  if (origLoopEndRange < k || k % L->getStride())
    return nullptr;

  // Update the new and original-loop's trip count.
  L->setEnd(k);

  CloneCtx map;
  Loop *L2 = (Loop *)L->clone(map);
  L2->setEnd(origLoopEndRange - k);
  L2->setName(newIndexName(L->getName(), "peeled", 0));

  // Update all of the indices in the program to refer to the combination of
  // two indices of the two loops.
  std::vector<IndexExpr *> indices;
  collectIndices(L2, indices, L2);
  for (auto *idx : indices) {
    auto *expr = new BinaryExpr(new ConstantExpr(k), new IndexExpr(L2),
                                BinaryExpr::BinOpKind::Add, L->getLoc());
    idx->replaceUseWith(expr);
  }

  // Insert the peeled loop after the original loop.
  ((Scope *)L->getParent())->insertAfterStmt(L2, L);
  return L2;
}

//--------------------------   Vectorization   -------------------------------//

/// \returns True if it is legal to vectorize some load/store with the indices
/// \p indices when vectorizing the loop \p L.
static bool mayVectorizeLoadStoreAccess(const std::vector<ExprHandle> &indices,
                                        Loop *L) {
  // Iterate over all of the indices and check if they allow vectorization.
  for (int i = 0, e = indices.size(); i < e; i++) {
    auto kind = getIndexAccessKind(indices[i].get(), L);
    bool isLastIndex = (i == e - 1);

    if (isLastIndex) {
      // The last index must be consecutive.
      if (kind != IndexAccessKind::Consecutive &&
          kind != IndexAccessKind::Uniform)
        return false;
    } else {
      // Tall other indices must be uniform.
      if (kind != IndexAccessKind::Uniform)
        return false;
    }
  }
  return true;
}

/// Collect the store instructions that use the indices.
/// \returns True if all roots were detected or False if there was a problem
/// analyzing the function.
static bool collectStoreSites(std::set<StoreStmt *> &stores,
                              const std::vector<IndexExpr *> &indices) {
  for (auto *index : indices) {
    ASTNode *parent = index;
    // Look up the use-chain and look for the stores that uses this index.
    while (true) {
      if (StoreStmt *ST = dynamic_cast<StoreStmt *>(parent)) {
        stores.insert(ST);
        break;
      }
      parent = parent->getParent();
      if (!parent)
        return false;
    }
  }

  return true;
}

/// \returns True if it is possible to vectorize the expression \p E, which can
/// be a value to be stored, when vectorizing the dimension \p L.
static bool mayVectorizeExpr(Expr *E, Loop *L) {
  std::vector<LoadExpr *> loads;
  std::vector<StoreStmt *> stores;
  collectLoadStores(E, loads, stores);

  for (auto *ld : loads) {
    if (!mayVectorizeLoadStoreAccess(ld->getIndices(), L))
      return false;
  }

  return true;
}

/// \returns True if the store statement is vectorizable on index \p L.
static bool mayVectorizeStore(StoreStmt *S, Loop *L) {
  // Can't vectorize already vectorized stores.
  if (S->getValue()->getType().isVector())
    return false;
  // We must be able to vectorize the stored value.
  if (!mayVectorizeExpr(S->getValue(), L))
    return false;
  // Check if the indices allow us to vectorize the loop.
  return mayVectorizeLoadStoreAccess(S->getIndices(), L);
}

/// Vectorize the expression on the index \p L with vectorization factor \p vf.
/// \returns a new scalar or vectorized expression.
static Expr *vectorizeExpr(Expr *E, Loop *L, unsigned vf) {
  if (IndexExpr *IE = dynamic_cast<IndexExpr *>(E)) {
    // Don't touch indices that are not vectorized.
    if (IE->getLoop() != L) {
      return IE;
    }

    ExprType IndexTy(ElemKind::IndexTy, vf);
    return new IndexExpr(L, IndexTy);
  }

  // We can vectorize add/mul expressions if we can vectorize both sides.
  if (BinaryExpr *AE = dynamic_cast<BinaryExpr *>(E)) {
    auto *VL = vectorizeExpr(AE->getLHS(), L, vf);
    auto *VR = vectorizeExpr(AE->getRHS(), L, vf);

    // Broadcast one side if the other is a vector.
    if (VL->getType().isVector() != VR->getType().isVector()) {
      if (!VL->getType().isVector())
        VL = new BroadcastExpr(VL, vf);
      if (!VR->getType().isVector())
        VR = new BroadcastExpr(VR, vf);
    }
    return new BinaryExpr(VL, VR, AE->getKind(), AE->getLoc());
  }

  // Vectorize unary expressions.
  if (UnaryExpr *UE = dynamic_cast<UnaryExpr *>(E)) {
    auto *VL = vectorizeExpr(UE->getVal(), L, vf);
    return new UnaryExpr(VL, UE->getKind(), UE->getLoc());
  }

  // Check that the load remains consecutive when vectorizing \p L.
  if (LoadExpr *LE = dynamic_cast<LoadExpr *>(E)) {
    std::vector<IndexExpr *> idx;
    collectIndices(LE, idx, L);

    // If L is not used as an index inside the Load
    // then we should scalarize it.
    if (!idx.size()) {
      return E;
    }

    std::vector<Expr *> indices;
    for (auto &E : LE->getIndices())
      indices.push_back(E.get());

    // Create a new vectorized load.
    auto *VLE = new LoadExpr(LE->getDest(), indices, LE->getLoc());
    VLE->setType(ExprType(VLE->getType().getElementType(), vf));
    return VLE;
  }

  if (dynamic_cast<ConstantExpr *>(E) || dynamic_cast<ConstantFPExpr *>(E)) {
    return E;
  }

  assert(false && "Invalid expression");
  return nullptr;
}

/// Vectorize the store statement on the index \p L using the vectorization
/// factor \p vf.
static StoreStmt *vectorizeStore(StoreStmt *S, Loop *L, unsigned vf) {
  // Vectorize or broadcast the value to be saved.
  Expr *val = vectorizeExpr(S->getValue().get(), L, vf);
  if (!val->getType().isVector()) {
    val = new BroadcastExpr(val, vf);
  }

  std::vector<Expr *> indices;
  for (auto &E : S->getIndices())
    indices.push_back(E.get());

  return new StoreStmt(S->getDest(), indices, val, S->isAccumulate(),
                       S->getLoc());
}

bool bistra::vectorize(Loop *L, unsigned vf) {
  unsigned tripCount = L->getEnd();
  // The trip count must contain the vec-width and loop must not be vectorized.
  if (tripCount < vf || L->getStride() != 1) {
    return false;
  }

  // Check if we can handle all of the statements contained in the loop.
  auto stmts = collectStmts(L);
  for (auto *s : stmts) {
    if (dynamic_cast<StoreStmt *>(s))
      continue;
    if (dynamic_cast<IfRange *>(s))
      continue;
    if (dynamic_cast<Loop *>(s))
      continue;

    // We can't handle this kind of statement.
    return false;
  }

  // Collect the indices in the loop L that access the index of L.
  std::vector<IndexExpr *> indices;
  collectIndices(L, indices, L);

  std::set<StoreStmt *> stores;
  bool collected = collectStoreSites(stores, indices);
  if (!collected)
    return false;

  for (auto *S : stores) {
    if (!mayVectorizeStore(S, L)) {
      return false;
    }
  }

  // Transform the loop to divide the loop trip count.
  if (tripCount % vf) {
    ::peelLoop(L, tripCount - (tripCount % vf));
  }

  // Update the loop stride to reflect the vectorization factor.
  L->setStride(vf);

  // Update the stores in the program and the expressions they drive.
  for (auto *S : stores) {
    auto *handle = S->getOwnerHandle();
    handle->setReference(vectorizeStore(S, L, vf));
  }

  return true;
}

/// Widen (duplicate and update index) the store \p S. Using the loop index \p L
/// and the offset \p offsett. Example: A[i]=B[i] becomes A[i]=B[i]; A[i+1] =
/// B[i+1]
static void widenStore(StoreStmt *S, Loop *L, unsigned offset) {
  CloneCtx map;
  auto *dup = S->clone(map);

  // Update all of the indices in the statement to refer to the shifted index.
  std::vector<IndexExpr *> indices;
  collectIndices(dup, indices, L);

  for (auto *idx : indices) {
    // I -> (I + offset);
    auto *expr = new BinaryExpr(new IndexExpr(L), new ConstantExpr(offset),
                                BinaryExpr::BinOpKind::Add, S->getLoc());
    idx->replaceUseWith(expr);
  }

  // Insert the new store after the original store.
  ((Scope *)S->getParent())->insertAfterStmt(dup, S);
}

bool bistra::widen(Loop *L, unsigned wf) {
  assert(wf > 1 && wf < 1024 && "Unexpected widen factor");
  unsigned stride = L->getStride();
  unsigned newStride = stride * wf;

  std::vector<LoadLocalExpr *> lloads;
  std::vector<StoreLocalStmt *> lstores;
  collectLocals(L, lloads, lstores, nullptr);
  // We can't handle local loads/stores in this optimization.
  if (lloads.size() || lstores.size())
    return false;

  unsigned tripCount = L->getEnd();
  // The trip count must contain the vec-width and loop must not be vectorized.
  if (tripCount < (newStride)) {
    return false;
  }

  std::vector<IndexExpr *> indices;
  collectIndices(L, indices, L);

  std::set<StoreStmt *> stores;
  bool collected = collectStoreSites(stores, indices);
  if (!collected)
    return false;

  // Transform the loop to divide the loop trip count.
  if (tripCount % newStride) {
    ::peelLoop(L, tripCount - (tripCount % (newStride)));
  }

  // Widen the stores by duplicating them X times, updating the references to
  // the induction variable.
  for (auto *S : stores) {
    // Duplicate and update the store WF times.
    for (int i = 1; i < wf; i++) {
      // We generate the dups in reverse order because we insert the code right
      // after the original store, so reverse order creates consecutive orders
      // that are easy to read and could help the compiler generate better code.
      widenStore(S, L, (wf - i) * stride);
    }
  }

  // Update the loop stride to reflect the widen factor. Notice that we may
  // widen vectorized loops, so we need to multiply the widen factor by the
  // current stride.
  L->setStride(newStride);
  return true;
}

static bool hoistLoads(Program *p, Loop *L) {
  std::vector<LoadExpr *> loads;
  std::vector<StoreStmt *> stores;
  collectLoadStores(L, loads, stores);

  // Abort if there are buffer dependencies between loads and stores.
  if (!areLoadsStoresDisjoint(loads, stores))
    return false;

  // Only hoist from innermost loops to prevent hoisting from internal loops
  // with index dependency.
  for (auto &s : L->getBody()) {
    if (!dynamic_cast<StoreStmt *>(s.get())) {
      return false;
    }
  }

  Scope *parentScope = (Scope *)L->getParent();

  for (auto *ld : loads) {
    // Don't hoist loads that depend on the loop index.
    if (dependsOnLoop(ld, L))
      continue;

    // Add a temporary local variable.
    auto ty = ld->getType();
    auto *var = p->addTempVar(ld->getDest()->getName(), ty);

    CloneCtx map;
    // Load the memory into a local before the loop.
    auto *save = new StoreLocalStmt(var, ld->clone(map), false, ld->getLoc());
    parentScope->insertBeforeStmt(save, L);

    // Load the value during the loop.
    ld->replaceUseWith(new LoadLocalExpr(var, ld->getLoc()));
  }

  return false;
}

static bool sinkStores(Program *p, Loop *L) {
  bool changed = false;
  std::vector<LoadExpr *> loads;
  std::vector<StoreStmt *> stores;
  collectLoadStores(L, loads, stores);
  // Abort if there are buffer dependencies between loads and stores.
  if (!areLoadsStoresDisjoint(loads, stores))
    return false;

  // Only sink from innermost loops to prevent sinking from internal loops
  // with index dependency.
  for (auto &s : L->getBody()) {
    if (!dynamic_cast<StoreStmt *>(s.get())) {
      return false;
    }
  }

  Scope *parentScope = (Scope *)L->getParent();

  for (auto *st : stores) {
    // Don't hoist stores that depend on the loop index.
    bool dep = false;
    for (auto &idx : st->getIndices()) {
      dep |= dependsOnLoop(idx.get(), L);
    }
    if (dep)
      continue;

    // Add a temporary local variable.
    auto ty = st->getValue()->getType();
    auto *var = p->addTempVar(st->getDest()->getName(), ty);

    // Zero the accumulator before the loop.
    CloneCtx map;
    auto *init = new StoreLocalStmt(var, getZeroExpr(ty), false, st->getLoc());
    parentScope->insertBeforeStmt(init, L);

    // Store the variable after the loop.
    auto *flush = new StoreStmt(st->getDest(), st->cloneIndicesPtr(map),
                                new LoadLocalExpr(var, st->getLoc()),
                                st->isAccumulate(), st->getLoc());
    parentScope->insertAfterStmt(flush, L);

    // Save into the temporary local variable.
    auto *save = new StoreLocalStmt(var, st->getValue()->clone(map),
                                    st->isAccumulate(), st->getLoc());
    L->replaceStmt(save, st);
    changed = true;
  }

  return changed;
}

bool bistra::promoteLICM(Program *p) {
  std::vector<Loop *> loops;
  collectLoops(p, loops);

  bool changed = false;
  for (auto &L : loops) {
    changed |= hoistLoads(p, L);
    changed |= sinkStores(p, L);
  }

  return changed;
}

bool bistra::applyPragmaCommand(const PragmaCommand &pc) {
  switch (pc.kind_) {
  case PragmaCommand::PragmaKind::vectorize:
    return ::vectorize(pc.L_, pc.param_);

  case PragmaCommand::PragmaKind::unroll:
    return ::unrollLoop(pc.L_, pc.param_);

  case PragmaCommand::PragmaKind::widen:
    return ::widen(pc.L_, pc.param_);

  case PragmaCommand::PragmaKind::tile:
    return ::tile(pc.L_, pc.param_);

  case PragmaCommand::peel:
    return ::peelLoop(pc.L_, pc.param_);
    break;
  case PragmaCommand::PragmaKind::hoist:
    return ::hoist(pc.L_, pc.param_);

  case PragmaCommand::other:
    assert(false && "Invalid pragma");
    return false;
  }

  assert(false && "Unhandled pragma");
  return false;
}
