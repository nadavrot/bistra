#ifndef BISTRA_TRANSFORMS_TRANSFORMS_H
#define BISTRA_TRANSFORMS_TRANSFORMS_H

#include "bistra/Program/Program.h"
#include "bistra/Program/Types.h"

namespace bistra {

struct PragmaCommand;

/// Tile the execution of loop \p L with \p blockSize iterations.
/// \returns the new new Loop if the transform worked or nullptr.
Loop *tile(Loop *L, unsigned blockSize);

/// Split the loop to multiple consecutive loops.
/// \returns True if the transform worked.
bool split(Loop *L);

/// Split the loop to multiple consecutive loops but keep non-scope statements
/// together. This creates a structure that allows loop interchange because
/// loops are either innermost loops with content or contain one other nested
/// loop.
/// \returns True if the transform worked.
bool splitScopes(Loop *L);

/// Distribute all of the loops in \p s.
bool ditributeAllLoops(Scope *s);

/// Try to hoist the loop \p level levels up.
bool hoist(Loop *L, unsigned levels);

/// Try to sink the loop \p level levels up.
/// \returns True if the loop was sunk at least one level.
bool sink(Loop *L, unsigned levels);

/// Unroll the loop \p L by copying its body up to \p maxTripCount times.
/// \returns True if the transform worked.
bool unrollLoop(Loop *L, unsigned maxTripCount);

/// Splits the loop into two consecutive loops in the ranges [0..k], [k..n];
/// Negative peel values \p k means peel from the end.
/// Return the new loop if the transform worked or nullptr.
Loop *peelLoop(Loop *L, int k);

/// Try to vectorize the loop \p L for the vectorization factor \p vf.
bool vectorize(Loop *L, unsigned vf);

/// Widen the loop by the factor \p wf. Widening is similar to vectorization
/// because we perform more work on each iteration. It is also similar unrolling
/// Each store site that uses the induction variable is duplicated.
/// Example: A[i] = 3 becomes A[i] = 3; A[i+1] = 3;
bool widen(Loop *L, unsigned wf);

/// Fuse the loop L, with the following loop. For example:
///   for (i in 0..100) {A[i] = 3;}
///   for (j in 0..100) {B[j] = 9;}
/// becomes
///   for (i in 0..100) {A[i] = 3; A[j] = 9;}
/// \returns True if the loop was modified.
bool fuse(Loop *L, unsigned levels);

/// Promote some memory usage from the memory to local variables.
/// \returns true if the program was modified.
bool promoteLICM(Program *p);

/// Apply the pragma command \p pc to the requested loop.
/// \returns true if the program was modified.
bool applyPragmaCommand(const PragmaCommand &pc);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_TRANSFORMS_H
