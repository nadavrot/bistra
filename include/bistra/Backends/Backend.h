#ifndef BISTRA_BACKENDS_BACKEND_H
#define BISTRA_BACKENDS_BACKEND_H

#include "bistra/Program/Program.h"

namespace bistra {

class Backend {
public:
  virtual ~Backend() = default;

  /// Generate code for the program \p P and save it at path \p path.
  /// Emit an object file, or source if \p isSrc is set.
  /// If \p iter is non-zero then emit a benchmark procedure that runs \p iter
  /// iterations.
  virtual void emitProgramCode(Program *p, const std::string &path, bool isSrc,
                               int iter) = 0;

  /// Compile and evaluate the performance of the program \p p.
  /// Execute \p iter number of iterations.
  /// \returns the time in seconds it took to execute the proogram.
  virtual double evaluateCode(Program *p, unsigned iter) = 0;

  /// Compile and run the program \p p on the tensors that are stored
  /// consecutively in \p mem.
  virtual void runOnce(Program *p, void *mem) = 0;
};

} // namespace bistra

#endif // BISTRA_BACKENDS_BACKEND_H
