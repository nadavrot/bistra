#include "bistra/Backends/Backend.h"
#include "bistra/Backends/LLVMBackend/LLVMBackend.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include "llvm/TargetParser/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Support/Error.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"

#include "JIT.h"


#include <utility>

using namespace bistra;

void LLVMBackend::optimize(llvm::TargetMachine &TM, llvm::Module *M) {
  M->setDataLayout(TM.createDataLayout());
  M->setTargetTriple(TM.getTargetTriple().normalize());

// Create the analysis managers.
// These must be declared in this order so that they are destroyed in the
// correct order due to inter-analysis-manager references.
llvm::LoopAnalysisManager LAM;
llvm::FunctionAnalysisManager FAM;
llvm::CGSCCAnalysisManager CGAM;
llvm::ModuleAnalysisManager MAM;

// Create the new pass manager builder.
// Take a look at the PassBuilder constructor parameters for more
// customization, e.g. specifying a TargetMachine or various debugging
// options.
llvm::PassBuilder PB;

// Register all the basic analyses with the managers.
PB.registerModuleAnalyses(MAM);
PB.registerCGSCCAnalyses(CGAM);
PB.registerFunctionAnalyses(FAM);
PB.registerLoopAnalyses(LAM);
PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

// Create the pass manager.
// This one corresponds to a typical -O2 optimization pipeline.
llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);

// Optimize the IR!
MPM.run(*M, MAM);
}
static llvm::ExitOnError ExitOnErr;

LLVMBackend::LLVMBackend() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();
}

LLVMBackend::~LLVMBackend() { }

llvm::TargetMachine &LLVMBackend::getTargetMachine() {
  using namespace llvm;

  auto CPU = "generic";
  auto Features = "";
  auto TargetTriple = sys::getProcessTriple();
  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);
  assert(Target && "Can't initialize the target");

  llvm::TargetOptions opt;
  auto RM = std::optional<llvm::Reloc::Model>();
  return *Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);
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

double LLVMBackend::run(std::unique_ptr<llvm::Module> M, 
                        std::unique_ptr<llvm::LLVMContext> ctx,
                        void *mem,
                        unsigned iter) {
  
  using namespace llvm;
  llvm::ExitOnError ExitOnErr;
  auto J = ExitOnErr(orc::LLJITBuilder().create());
  llvm::orc::ThreadSafeModule TSM(std::move(M), std::move(ctx));

  ExitOnErr(J->addIRModule(std::move(TSM)));

  // Look up the JIT'd function, cast it to a function pointer, then call it.
  auto ExprSymbol = ExitOnErr(J->lookup("benchmark"));
  
  assert(ExprSymbol && "Function not found");

  auto addr = ExprSymbol.toPtr<void (*)(void *)>();

  double timeSpent = 0.0;

  if (addr) {
    void (*call)(void *) = addr;
    clock_t begin = clock();
    call(mem);
    clock_t end = clock();
    timeSpent += (double)(end - begin) / CLOCKS_PER_SEC;
  }

  // Don't warn on the unused function that is used for verification.
  (void)crcBuffer;

  return timeSpent / iter;
}

void LLVMBackend::emitObject(llvm::Module *M, const std::string &path) {
  std::error_code EC;
  llvm::raw_fd_ostream dest(path, EC);

  if (EC) {
    llvm::errs() << "Could not open file: " << EC.message();
    return;
  }

  llvm::legacy::PassManager pass;
  auto FileType = llvm::CodeGenFileType::ObjectFile;

  if (getTargetMachine().addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    llvm::errs() << "TargetMachine can't emit a file of this type";
    return;
  }

  pass.run(*M);
  dest.flush();
}
