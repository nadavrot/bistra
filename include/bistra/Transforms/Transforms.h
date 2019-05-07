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

/// Widen the loop by the factor \p wf. Widening is similar to vectorization
/// because we perform more work on each iteration. It is also similar unrolling
/// Each store site that uses the induction variable is duplicated.
/// Example: A[i] = 3 becomes A[i] = 3; A[i+1] = 3;
bool widen(Loop *L, unsigned wf);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_TRANSFORMS_H
