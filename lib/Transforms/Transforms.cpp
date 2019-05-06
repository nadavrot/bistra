#include "bistra/Transforms/Transforms.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include <set>

using namespace bistra;

bool bistra::tile(Loop *L, unsigned blockSize) {
  // Trip count must divide the block size.
  if (L->getEnd() % blockSize)
    return false;

  // Update the original-loop's trip count.
  L->setEnd(L->getEnd() / blockSize);

  // Create a new loop.
  auto *B = new Loop(L->getName() + "_tile_" + std::to_string(blockSize),
                     blockSize, 1);

  // Insert the new loop by moving the content of the original loop.
  B->takeContent(L);
  L->addStmt(B);

  // Update all of the indices in the program to refer to the combination of
  // two indices of the two loops.
  std::vector<IndexExpr *> indices;
  collectIndices(L, indices);

  for (auto *idx : indices) {
    // I -> (I * bs) + I.tile;
    auto *mul = new MulExpr(new IndexExpr(L), new ConstantExpr(blockSize));
    auto *expr = new AddExpr(new IndexExpr(B), mul);
    idx->replaceUseWith(expr);
  }

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
      collectIndices(newSt, indices);
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

bool bistra::peelLoop(Loop *L, unsigned k) {
  unsigned origTripCount = L->getEnd();
  // Trip count must be smaller than the partition size.
  if (origTripCount < k)
    return false;

  // Update the new and original-loop's trip count.
  L->setEnd(k);

  CloneCtx map;
  Loop *L2 = (Loop *)L->clone(map);
  L2->setEnd(origTripCount - k);
  L2->setName(L->getName() + "_peeled");

  // Update all of the indices in the program to refer to the combination of
  // two indices of the two loops.
  std::vector<IndexExpr *> indices;
  collectIndices(L2, indices);
  for (auto *idx : indices) {
    if (idx->getLoop() == L2) {
      auto *expr = new AddExpr(new ConstantExpr(k), new IndexExpr(L2));
      idx->replaceUseWith(expr);
    }
  }

  // Insert the peeled loop after the original loop.
  ((Scope *)L->getParent())->insertAfterStmt(L2, L);
  return true;
}

//--------------------------   Vectorization   -------------------------------//

static bool mayVectorizeLastIndex(Expr *E, Loop *L) {
  // A list of expressions to process.
  std::vector<Expr *> worklist = {E};

  // Count the number of times that the index is used. (must be 1!)
  unsigned loopIndexFound = 0;

  while (worklist.size()) {
    Expr *E = worklist.back();
    worklist.pop_back();

    // It is okay to access the loop index.
    if (IndexExpr *IE = dynamic_cast<IndexExpr *>(E)) {
      if (IE->getLoop() == L) {
        loopIndexFound++;
        continue;
      }
      // Other indices are okay to access as long as they are scalar.
      if (IE->getLoop()->getVF() != 1)
        return false;
    }

    // Addition expressions are okay because they don't scale the index.
    if (AddExpr *AE = dynamic_cast<AddExpr *>(E)) {
      worklist.push_back(AE->getLHS());
      worklist.push_back(AE->getRHS());
      continue;
    }
    // Unknown expression. Aborting.
    return false;
  }
  return true;
}

/// Check if we can vectorize a load/store access for a coordinate that is
/// not the last consecutive one. Rule: must not use the vectorized loop index.
static bool mayVectorizeNonLastIndex(Expr *E, Loop *L) {
  std::vector<IndexExpr *> collected;
  collectIndices(E, collected);
  // Check that the non-consecutive dims don't access the vectorized index.
  for (auto &idx : collected) {
    assert(idx->getLoop()->getVF() == 1 && "vectorizing on the wrong dim");
    if (idx->getLoop() == L)
      return false;
  }

  return true;
}

/// \returns True if it is legal to vectorize some load/store with the indices
/// \p indices when vectorizing the loop \p L.
static bool mayVectorizeLoadStoreAccess(const std::vector<ExprHandle> &indices,
                                        Loop *L) {
  // Iterate over all of the indices except for the last index.
  for (int i = 0, e = indices.size() - 1; i < e; i++) {
    std::vector<IndexExpr *> collected;
    collectIndices(indices[i].get(), collected);
    if (!mayVectorizeNonLastIndex(indices[i].get(), L))
      return false;
  }

  // Check the last "consecutive" index.
  if (!mayVectorizeLastIndex(indices[indices.size() - 1].get(), L))
    return false;

  return true;
}

/// \returns True if it is possible to vectorize the expression \p E, which can
/// be a value to be stored, when vectorizing the dimension \p L.
static bool mayVectorizeExpr(Expr *E, Loop *L) {
  if (IndexExpr *IE = dynamic_cast<IndexExpr *>(E)) {
    return true;
  }

  // We can vectorize add/mul expressions if we can vectorize both sides.
  if (BinaryExpr *AE = dynamic_cast<BinaryExpr *>(E)) {
    return (mayVectorizeExpr(AE->getLHS(), L) &&
            mayVectorizeExpr(AE->getRHS(), L));
  }

  // Check that the load remains consecutive when vectorizing \p L.
  if (LoadExpr *LE = dynamic_cast<LoadExpr *>(E)) {
    return mayVectorizeLoadStoreAccess(LE->getIndices(), L);
  }

  if (dynamic_cast<ConstantExpr *>(E) || dynamic_cast<ConstantFPExpr *>(E)) {
    return true;
  }

  return false;
}

/// Collect the store instructions that use the indices.
/// \returns True if all roots were detected or False if there was a problem
/// analyzing the function.
/// TODO: add support for non-store uses of index, such as if-conditions.
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

/// Vectorize the expression on the index \p L.
/// \returns a new scalar or vectorized expression.
static Expr *vectorizeExpr(Expr *E, Loop *L) {
  if (IndexExpr *IE = dynamic_cast<IndexExpr *>(E)) {
    ExprType IndexTy(ElemKind::IndexTy, L->getVF());
    return new IndexExpr(L, IndexTy);
  }

  // We can vectorize add/mul expressions if we can vectorize both sides.
  if (BinaryExpr *AE = dynamic_cast<BinaryExpr *>(E)) {
    auto *VL = vectorizeExpr(AE->getLHS(), L);
    auto *VR = vectorizeExpr(AE->getRHS(), L);

    // Broadcast one side if the other is a vector.
    if (VL->getType().isVector() != VR->getType().isVector()) {
      if (!VL->getType().isVector())
        VL = new BroadcastExpr(VL, L->getVF());
      if (!VR->getType().isVector())
        VR = new BroadcastExpr(VR, L->getVF());
    }

    if (dynamic_cast<AddExpr *>(E)) {
      return new AddExpr(VL, VR);
    } else if (dynamic_cast<AddExpr *>(E)) {
      return new MulExpr(VL, VR);
    } else {
      assert(false && "Invalid binary operator");
    }
  }

  // Check that the load remains consecutive when vectorizing \p L.
  if (LoadExpr *LE = dynamic_cast<LoadExpr *>(E)) {
    std::vector<Expr *> indices;
    for (auto &E : LE->getIndices())
      indices.push_back(vectorizeExpr(E.get(), L));
    return new LoadExpr(LE->getDest(), indices);
  }

  if (dynamic_cast<ConstantExpr *>(E) || dynamic_cast<ConstantFPExpr *>(E)) {
    return E;
  }

  assert(false && "Invalid expression");
  return nullptr;
}

/// Vectorize the store statement on the index \p L.
static StoreStmt *vectorizeStore(StoreStmt *S, Loop *L) {
  // Vectorize or broadcast the value to be saved.
  Expr *val = vectorizeExpr(S->getValue().get(), L);
  if (!val->getType().isVector()) {
    val = new BroadcastExpr(val, L->getVF());
  }

  std::vector<Expr *> indices;
  for (auto &E : S->getIndices()) {
    indices.push_back(vectorizeExpr(E.get(), L));
  }
  return new StoreStmt(S->getDest(), indices, val, S->isAccumulate());
}

bool bistra::vectorize(Loop *L, unsigned vf) {
  // The vectorization factor must divide the loop trip count.
  if (L->getEnd() % vf) {
    return false;
  }

  std::vector<IndexExpr *> indices;
  collectIndices(L, indices);

  std::set<StoreStmt *> stores;
  bool collected = collectStoreSites(stores, indices);
  if (!collected)
    return false;

  for (auto *S : stores) {
    if (!mayVectorizeStore(S, L)) {
      return false;
    }
  }

  // Update the loop vectorization factor.
  L->setVF(vf);

  // Update the stores in the program and the expressions they drive.
  for (auto *S : stores) {
    auto *handle = S->getOwnerHandle();
    handle->setReference(vectorizeStore(S, L));
  }

  return true;
}
