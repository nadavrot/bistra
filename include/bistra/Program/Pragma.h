#ifndef BISTRA_PROGRAM_PRAGMA_H
#define BISTRA_PROGRAM_PRAGMA_H

#include "bistra/Base/Base.h"

#include <string>

namespace bistra {
class Loop;

/// Represents a pragma command that the user requested to apply to some loop.
struct PragmaCommand {
  enum PragmaKind { vectorize, unroll, widen, tile, peel, hoist, fuse, other };

  PragmaCommand(PragmaKind kind, const std::string &loopName,
                const std::string &newName, int param, DebugLoc loc)
      : kind_(kind), loopName_(loopName), newName_(newName), param_(param),
        loc_(loc) {}

  /// The kind of the pragma.
  PragmaKind kind_;
  /// A name for the loop to transform.
  std::string loopName_;
  /// An optional new name for the loop.
  std::string newName_;
  /// The parameter for the pragma.
  int param_;
  /// The location of the pragma.
  DebugLoc loc_;
};

} // namespace bistra

#endif // BISTRA_PROGRAM_PRAGMA_H
