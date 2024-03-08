#ifndef BISTRA_BACKENDS_LLVMBACKEND_LLVMBACKEND_H
#define BISTRA_BACKENDS_LLVMBACKEND_LLVMBACKEND_H

#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"

namespace llvm {
namespace orc {
class SimpleJIT;
}
class Module;
class LLVMContext;
class TargetMachine;
} // namespace llvm

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

  virtual void emitProgramCode(Program *p, const std::string &path, bool isSrc,
                               int iter) override;

  double run(std::unique_ptr<llvm::Module> M,
             std::unique_ptr<llvm::LLVMContext> ctx,
              void *mem, unsigned iter);

  virtual double evaluateCode(Program *p, unsigned iter) override;

  virtual void runOnce(Program *p, void *mem) override;

  virtual unsigned getNumRegisters() const override { return 16; }

  virtual unsigned getRegisterWidth() const override { return 8; }
};

} // namespace bistra

#endif // BISTRA_BACKENDS_LLVMBACKEND_LLVMBACKEND_H
