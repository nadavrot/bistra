#include "bistra/Backends/Backend.h"
#include "bistra/Backends/LLVMBackend/LLVMBackend.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "JIT.h"

using namespace bistra;

void LLVMBackend::optimize(llvm::TargetMachine &TM, llvm::Module *M) {
  M->setDataLayout(TM.createDataLayout());
  M->setTargetTriple(TM.getTargetTriple().normalize());

  llvm::PassManagerBuilder PMB;
  PMB.OptLevel = 2;
  PMB.SizeLevel = 1;
  PMB.LoopVectorize = false;
  PMB.SLPVectorize = false;

  llvm::legacy::FunctionPassManager FPM(M);
  llvm::legacy::PassManager PM;

  // Add internal analysis passes from the target machine.
  PM.add(llvm::createTargetTransformInfoWrapperPass(TM.getTargetIRAnalysis()));
  FPM.add(llvm::createTargetTransformInfoWrapperPass(TM.getTargetIRAnalysis()));

  PMB.populateFunctionPassManager(FPM);
  PMB.populateModulePassManager(PM);
  FPM.doInitialization();
  PM.run(*M);
  for (auto &FF : *M) {
    FPM.run(FF);
  }
  FPM.doFinalization();
  PM.run(*M);
}

LLVMBackend::LLVMBackend() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();
  JIT = new llvm::orc::SimpleJIT();
}

LLVMBackend::~LLVMBackend() { delete JIT; }

llvm::TargetMachine &LLVMBackend::getTargetMachine() {
  return JIT->getTargetMachine();
}

/// Calculate some checksum for the buffer.
static unsigned crcBuffer(float *A, int len) {
  // This can warp and it's okay.
  unsigned sum = 0;
  for (int i = 0; i < len; i++) {
    sum += A[i];
  }
  return sum;
}

double LLVMBackend::run(std::unique_ptr<llvm::Module> M, void *mem,
                        unsigned iter) {
  auto H = JIT->addModule(std::move(M));

  auto ExprSymbol = JIT->findSymbol("benchmark");
  assert(ExprSymbol && "Function not found");

  auto addr = ExprSymbol.getAddress();

  double timeSpent = 0.0;

  if (addr) {
    void (*call)(void *) = (void (*)(void *))addr.get();
    clock_t begin = clock();
    call(mem);
    clock_t end = clock();
    timeSpent += (double)(end - begin) / CLOCKS_PER_SEC;
  }

  // Don't warn on the unused function that is used for verification.
  (void)crcBuffer;

  JIT->removeModule(H);

  return timeSpent / iter;
}

void LLVMBackend::emitObject(llvm::Module *M, const std::string &path) {
  std::error_code EC;
  llvm::raw_fd_ostream dest(path, EC, llvm::sys::fs::F_None);

  if (EC) {
    llvm::errs() << "Could not open file: " << EC.message();
    return;
  }

  llvm::legacy::PassManager pass;
  auto FileType = llvm::TargetMachine::CGFT_ObjectFile;

  if (getTargetMachine().addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    llvm::errs() << "TargetMachine can't emit a file of this type";
    return;
  }

  pass.run(*M);
  dest.flush();
}
