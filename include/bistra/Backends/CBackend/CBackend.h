#ifndef BISTRA_BACKENDS_CBACKEND_CBACKEND_H
#define BISTRA_BACKENDS_CBACKEND_CBACKEND_H

#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"

namespace bistra {

class CBackend : public Backend {
public:
  virtual std::string emitProgramCode(Program *p) override;

  virtual std::string emitBenchmarkCode(Program *p, unsigned iter) override;

  virtual double evaluateCode(Program *p, unsigned iter) override;
};

} // namespace bistra

#endif // BISTRA_BACKENDS_CBACKEND_CBACKEND_H
