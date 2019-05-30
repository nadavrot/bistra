#include "bistra/Backends/LLVMBackend/LLVMBackend.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

using namespace bistra;

class LLVMEmitter {
  llvm::LLVMContext ctx_;
  llvm::IRBuilder<> builder_;
  std::unique_ptr<llvm::Module> M_;
  std::map<std::string, llvm::Value *> namedValues_;
  llvm::Function *func_;

  llvm::Type *int64Ty_;
  llvm::Constant *int64Zero_;

public:
  LLVMEmitter() : builder_(ctx_) {
    int64Ty_ = llvm::Type::getInt64Ty(ctx_);
    int64Zero_ = llvm::Constant::getNullValue(int64Ty_);
    M_ = std::make_unique<llvm::Module>("", ctx_);
  }

  llvm::Module *getModule() const { return M_.get(); }

  llvm::Function *emitPrototype(Program *p) {
    std::vector<llvm::Type *> argListType;

    // Construct the types for the argument list.
    for (auto *arg : p->getArgs()) {
      switch (arg->getType()->getElementType()) {
      case ElemKind::Float32Ty:
        argListType.push_back(llvm::Type::getFloatTy(ctx_));
        break;
      default:
        assert(false && "Invalid parameter");
      }
    }

    // Make the function type:  double(double,double) etc.
    llvm::FunctionType *FT = llvm::FunctionType::get(
        llvm::Type::getVoidTy(ctx_), argListType, false);

    llvm::Function *F = llvm::Function::Create(
        FT, llvm::Function::ExternalLinkage, p->getName(), M_.get());

    // Set names for all arguments.
    unsigned idx = 0;
    for (auto &arg : F->args())
      arg.setName(p->getArg(idx++)->getName());

    return F;
  }
  void emit(Loop *L) {
    auto *index = builder_.CreateAlloca(int64Ty_, 0, L->getName());
    builder_.CreateStore(int64Zero_, index);

    llvm::BasicBlock *header = llvm::BasicBlock::Create(ctx_, "header", func_);
    llvm::BasicBlock *body = llvm::BasicBlock::Create(ctx_, "body", func_);
    llvm::BasicBlock *nextIter = llvm::BasicBlock::Create(ctx_, "next", func_);
    llvm::BasicBlock *exit = llvm::BasicBlock::Create(ctx_, "exit", func_);

    builder_.CreateBr(header);
    builder_.SetInsertPoint(header);
    auto *idxVal = builder_.CreateLoad(int64Ty_, index, L->getName());

    auto upperBoundAP = llvm::APInt(64, L->getEnd());
    auto upperBound = llvm::Constant::getIntegerValue(int64Ty_, upperBoundAP);
    auto *cmp = builder_.CreateICmpSLE(idxVal, upperBound);
    builder_.CreateCondBr(cmp, body, exit);

    builder_.SetInsertPoint(nextIter);
    auto *idxVal2 = builder_.CreateLoad(int64Ty_, index, L->getName());
    auto step = llvm::APInt(64, L->getStride());
    auto *stride = llvm::Constant::getIntegerValue(int64Ty_, step);
    auto *plusStride = builder_.CreateAdd(idxVal2, stride);
    builder_.CreateStore(plusStride, index);
    builder_.CreateBr(header);

    builder_.SetInsertPoint(body);
    for (auto &s : L->getBody()) {
      emit(s);
    }
    builder_.CreateBr(nextIter);
    builder_.SetInsertPoint(exit);
  }

  void emit(Stmt *S) {
    if (Loop *L = dynamic_cast<Loop *>(S)) {
      return emit(L);
    }
  }

  llvm::Function *emit(Program *p) {
    func_ = emitPrototype(p);

    if (!func_)
      return nullptr;

    // Create a new basic block to start insertion into.
    llvm::BasicBlock *BB = llvm::BasicBlock::Create(ctx_, "entry", func_);
    builder_.SetInsertPoint(BB);

    // Record the function arguments in the NamedValues map.
    namedValues_.clear();
    for (auto &arg : func_->args())
      namedValues_[arg.getName()] = &arg;

    for (auto &stmt : p->getBody()) {
      emit(stmt);
    }

    builder_.CreateRetVoid();
    func_->print(llvm::outs());
    return func_;
  }
};

std::string LLVMBackend::emitProgramCode(Program *p) { return ""; }

std::string LLVMBackend::emitBenchmarkCode(Program *p, unsigned iter) {
  return "";
}

double LLVMBackend::evaluateCode(Program *p, unsigned iter) {
  LLVMEmitter EE;
  EE.emit(p);

  return 0;
}
