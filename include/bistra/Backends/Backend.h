#ifndef BISTRA_BACKENDS_BACKEND_H
#define BISTRA_BACKENDS_BACKEND_H

#include "bistra/Program/Program.h"

namespace bistra {

class Backend {
public:
  virtual ~Backend() = default;

  /// Generate code for the program \p p and return its string representation.
  virtual std::string emitCode(Program &p) = 0;
  /// Compile and evaluate the performance of the program \p p.
  /// Execute \p iter number of iterations.
  /// \returns the number of milliseconds it took to run the proogram.
  virtual unsigned evaluateCode(Program &p, unsigned iter) = 0;
};

} // namespace bistra

#endif // BISTRA_BACKENDS_BACKEND_H
