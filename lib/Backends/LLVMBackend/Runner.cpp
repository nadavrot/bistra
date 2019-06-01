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

static std::unique_ptr<llvm::orc::SimpleJIT> TheJIT;

static void optimize(llvm::TargetMachine &TM, llvm::Module *M) {
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

  M->print(llvm::outs(), 0);
}

void LLVMBackend::run(llvm::Module *M) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  auto theJIT = llvm::make_unique<llvm::orc::SimpleJIT>();
  auto &TM = theJIT->getTargetMachine();
  llvm::outs() << TM.getTargetCPU() << "\n";
  llvm::outs() << TM.getTargetFeatureString() << "\n";

  optimize(TM, M);

  std::unique_ptr<llvm::Module> MU = std::unique_ptr<llvm::Module>(M);
  auto H = theJIT->addModule(std::move(MU));

  auto ExprSymbol = TheJIT->findSymbol("program");
  assert(ExprSymbol && "Function not found");

  TheJIT->removeModule(H);
}
