#ifndef BISTRA_BACKENDS_LLVMBACKEND_LLVMBACKEND_H
#define BISTRA_BACKENDS_LLVMBACKEND_LLVMBACKEND_H

#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"

namespace llvm {
namespace orc {
class SimpleJIT;
}
class Module;
class TargetMachine;
}

namespace bistra {

class LLVMBackend : public Backend {
  /// An instance of the ORC JIT. Notice that we don't use unique_ptr here
  /// to work around and create the rtti barrier between our code and LLVM.
  llvm::orc::SimpleJIT *JIT;

  /// Optimize the module \p M.
  void optimize(llvm::TargetMachine &TM, llvm::Module *M);

  /// \returns the native target machine.
  llvm::TargetMachine &getTargetMachine();

public:
  LLVMBackend();
  ~LLVMBackend();

  void emitObject(llvm::Module *M, const std::string &path);

  virtual std::string emitProgramCode(Program *p) override;

  virtual std::string emitBenchmarkCode(Program *p, unsigned iter) override;

  double run(std::unique_ptr<llvm::Module> M, size_t memSize, unsigned iter);

  virtual double evaluateCode(Program *p, unsigned iter) override;
};

} // namespace bistra

#endif // BISTRA_BACKENDS_LLVMBACKEND_LLVMBACKEND_H
