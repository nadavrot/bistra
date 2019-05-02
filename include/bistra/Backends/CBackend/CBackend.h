#ifndef BISTRA_BACKENDS_CBACKEND_CBACKEND_H
#define BISTRA_BACKENDS_CBACKEND_CBACKEND_H

#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"

namespace bistra {

class CBackend : public Backend {
public:
  virtual std::string emitCode(Program &p) override;

  virtual unsigned evaluateCode(Program &p, unsigned iter) override {
    assert(false && "Unimplemented");
  }
};

} // namespace bistra

#endif // BISTRA_BACKENDS_CBACKEND_CBACKEND_H
