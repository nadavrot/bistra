#ifndef BISTRA_BACKENDS_LLVMBACKEND_LLVMBACKEND_H
#define BISTRA_BACKENDS_LLVMBACKEND_LLVMBACKEND_H

#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"

namespace llvm {
class Module;
}

namespace bistra {

class LLVMBackend : public Backend {
public:
  virtual std::string emitProgramCode(Program *p) override;

  virtual std::string emitBenchmarkCode(Program *p, unsigned iter) override;

  double run(std::unique_ptr<llvm::Module> M, size_t memSize, unsigned iter);

  virtual double evaluateCode(Program *p, unsigned iter) override;
};

} // namespace bistra

#endif // BISTRA_BACKENDS_LLVMBACKEND_LLVMBACKEND_H
