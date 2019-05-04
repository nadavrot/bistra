#ifndef BISTRA_TRANSFORMS_TRANSFORMS_H
#define BISTRA_TRANSFORMS_TRANSFORMS_H

#include "bistra/Program/Program.h"
#include "bistra/Program/Types.h"

namespace bistra {

/// Tile the execution of loop \p L in program \p P with \p blockSize
/// iterations. Return True if the transform worked.
bool tile(Program *P, Loop *L, unsigned blockSize);

/// Sink the loop \p L lower in the program.
/// Return True if the transform worked.
bool sinkLoop(Program *P, Loop *L);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_TRANSFORMS_H
