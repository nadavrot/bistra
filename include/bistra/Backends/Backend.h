#ifndef BISTRA_BACKENDS_BACKEND_H
#define BISTRA_BACKENDS_BACKEND_H

#include "bistra/Program/Program.h"

namespace bistra {

class Backend {
public:
  virtual ~Backend() = default;

  /// Generate code for the program \p P and return its string representation.
  virtual std::string emitProgramCode(Program *p) = 0;

  /// Generate a program that executes the program \p p \p iter times and
  /// reports the runtime.
  virtual std::string emitBenchmarkCode(Program *p, unsigned iter) = 0;

  /// Compile and evaluate the performance of the program \p p.
  /// Execute \p iter number of iterations.
  /// \returns the time in seconds it took to execute the proogram.
  virtual double evaluateCode(Program *p, unsigned iter) = 0;
};

} // namespace bistra

#endif // BISTRA_BACKENDS_BACKEND_H
