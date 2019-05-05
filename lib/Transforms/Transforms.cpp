#include "bistra/Transforms/Transforms.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

using namespace bistra;

bool bistra::tile(Program *P, Loop *L, unsigned blockSize) {
  // Trip count must divide the block size.
  if (L->getEnd() % blockSize)
    return false;

  // Update the original loop trip count.
  L->setEnd(L->getEnd() / blockSize);

  auto *B = new Loop(L->getName() + "_tile_" + std::to_string(blockSize),
                     blockSize, 1);

  B->takeContent(L);
  L->addStmt(B);

  std::vector<IndexExpr *> indices;
  collectIndices(L, indices);

  for (auto *idx : indices) {
    // I -> (I * bs) + I.tile;
    auto *mul = new MulExpr(new IndexExpr(L), new ConstantExpr(blockSize));
    auto *expr = new AddExpr(new IndexExpr(B), mul);
    idx->replaceUserWith(expr);
  }

  return true;
}

/// Sink the loop \p L lower in the program.
/// Return True if the transform worked.
bool bistra::sinkLoop(Program *P, Loop *L) { return true; }
