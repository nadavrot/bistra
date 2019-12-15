#ifndef BISTRA_OPTIMIZER_OPTIMIZER_H
#define BISTRA_OPTIMIZER_OPTIMIZER_H

#include "bistra/Program/Program.h"

#include <string>

namespace bistra {

class Program;
class Backend;

/// Construct an optimization pipeline and evaluate different configurations for
/// the program \p. Save intermediate results to \p filename.
/// \returns the best program.
Program *optimizeEvaluate(Backend &backend, Program *p,
                          const std::string &filename, bool isTextual,
                          bool isBytecode);

/// Try to statically optimize the program \p P based on heuristics.
/// \return the owned optimized program.
std::unique_ptr<Program> *optimizeStatic(Backend &backend, Program *p);

} // namespace bistra

#endif // BISTRA_OPTIMIZER_OPTIMIZER_H
