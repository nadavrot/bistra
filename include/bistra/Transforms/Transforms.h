#ifndef BISTRA_TRANSFORMS_TRANSFORMS_H
#define BISTRA_TRANSFORMS_TRANSFORMS_H

#include "bistra/Program/Program.h"
#include "bistra/Program/Types.h"

namespace bistra {

/// Tile the execution of loop \p L with \p blockSize iterations.
/// \returns the new new Loop if the transform worked or nullptr.
Loop *tile(Loop *L, unsigned blockSize);

/// Unroll the loop \p L by copying its body up to \p maxTripCount times.
/// Return True if the transform worked.
bool unrollLoop(Loop *L, unsigned maxTripCount);

/// Splits the loop into two consecutive loops in the ranges [0..k], [k..n];
/// Return the new loop if the transform worked or nullptr.
Loop *peelLoop(Loop *L, unsigned k);

/// Try to vectorize the loop \p L for the vectorization factor \p vf.
bool vectorize(Loop *L, unsigned vf);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_TRANSFORMS_H
