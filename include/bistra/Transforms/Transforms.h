#ifndef BISTRA_TRANSFORMS_TRANSFORMS_H
#define BISTRA_TRANSFORMS_TRANSFORMS_H

#include "bistra/Program/Program.h"
#include "bistra/Program/Types.h"

namespace bistra {

/// Tile the execution of loop \p L in program \p P with \p blockSize
/// iterations. Return a cloned function or nullptr if the transformation
/// failed.
Program *tile(Program *P, Loop *L, unsigned blockSize);

} // namespace bistra

#endif // BISTRA_TRANSFORMS_TRANSFORMS_H
