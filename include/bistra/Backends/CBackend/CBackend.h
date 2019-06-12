#ifndef BISTRA_BACKENDS_CBACKEND_CBACKEND_H
#define BISTRA_BACKENDS_CBACKEND_CBACKEND_H

#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"

namespace bistra {

class CBackend : public Backend {
public:
  virtual void emitProgramCode(Program *p, const std::string &path, bool isSrc,
                               int iter) override;

  virtual double evaluateCode(Program *p, unsigned iter) override;

  virtual void runOnce(Program *p, void *mem) override { assert(false); }
};

} // namespace bistra

#endif // BISTRA_BACKENDS_CBACKEND_CBACKEND_H
