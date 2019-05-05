#ifndef BISTRA_TRANSFORMS_TRANSFORMS_H
#define BISTRA_TRANSFORMS_TRANSFORMS_H

#include "bistra/Program/Program.h"
#include "bistra/Program/Types.h"

namespace bistra {

/// Tile the execution of loop \p L with \p blockSize iterations. Return True
/// if the transform worked.
bool tile(Loop *L, unsigned blockSize);

/// Sink the loop \p L lower in the program.
/// Return True if the transform worked.
bool sinkLoop(Loop *L);

/// Unroll the loop \p L by copying its body up to \p maxTripCount times.
/// Return True if the transform worked.
bool unrollLoop(Loop *L, unsigned maxTripCount);

/// Splits the loop into two consecutive loops in the ranges [0..k], [k..n];
/// Return True if the transform worked.
bool peelLoop(Loop *L, unsigned k);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_TRANSFORMS_H
