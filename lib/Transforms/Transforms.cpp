#include "bistra/Transforms/Transforms.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

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

/// Wraps the body of the loop \p L with a new loop that takes the shape of
/// \p old and redirect all of the old indices to the new loop.
static void insertLoopIntoLoop(Loop *L, Loop *old, Stmt *stmt) {
  Loop *newLoop =
      new Loop(old->getName() + "_sunk", old->getEnd(), old->getVF());

  // Replace all of the uses of the old loop with the new loop.
  std::vector<IndexExpr *> indices;
  collectIndices(old, indices);
  for (auto *idx : indices) {
    if (idx->getLoop() != old)
      continue;
    idx->replaceUseWith(new IndexExpr(newLoop));
    delete idx;
  }

  newLoop->takeContent(L);
  L->addStmt(newLoop);
}

/// Sink the loop \p L lower in the program.
/// Return True if the transform worked.
bool bistra::sinkLoop(Loop *L) {
  Scope *parent = dynamic_cast<Scope *>(L->getParent());
  assert(parent && "Unexpected parent shape");

  // For each one of the sub-statements in the loop body, check if they
  // are sinkable. TODO: add support for IF-statements.
  for (auto &ST : L->getBody()) {
    // We support loops.
    if (dynamic_cast<Loop *>(ST.get()))
      continue;
    // Unknown statement. Abort.
    return false;
  }

  // A list of statements that were wrapped in new loops.
  // TODO: add support for IF-statements.
  std::vector<Stmt *> loopedChildren;
  for (auto &ST : L->getBody()) {
    if (Loop *innerLoop = dynamic_cast<Loop *>(ST.get())) {
      for (auto &stmt : innerLoop->getBody()) {
        // Wrap the inner loop statement with a new loop and save it for later.
        insertLoopIntoLoop(innerLoop, L, stmt.get());
        loopedChildren.push_back(innerLoop);
      }
    }
  }

  // Insert the new loops before the original loop.
  for (auto *newLoop : loopedChildren) {
    parent->insertBeforeStmt(newLoop, L);
  }
  parent->removeStmt(L);
  return true;
}
