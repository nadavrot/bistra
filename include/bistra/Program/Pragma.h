#ifndef BISTRA_PROGRAM_PRAGMA_H
#define BISTRA_PROGRAM_PRAGMA_H

#include "bistra/Base/Base.h"

namespace bistra {
class Loop;

/// Represents a pragma command that the user requested to apply to some loop.
struct PragmaCommand {
  enum PragmaKind { vectorize, unroll, widen, tile, peel, hoist, fuse, other };

  PragmaCommand(PragmaKind kind, int param, Loop *L, DebugLoc loc)
      : kind_(kind), param_(param), L_(L), loc_(loc) {}

  /// The name of the pragma.
  PragmaKind kind_;
  /// The parameter for the pragma.
  int param_;
  /// The the loop that this pragma applies to.
  Loop *L_;
  /// The location of the pragma.
  DebugLoc loc_;
};

} // namespace bistra

#endif // BISTRA_PROGRAM_PRAGMA_H
