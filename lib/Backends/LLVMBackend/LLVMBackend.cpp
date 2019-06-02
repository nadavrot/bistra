#include "bistra/Backends/LLVMBackend/LLVMBackend.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include "llvm/Analysis/Passes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

using namespace bistra;

class LLVMEmitter {
  llvm::LLVMContext ctx_;
  llvm::IRBuilder<> builder_;
  std::unique_ptr<llvm::Module> M_;
  std::map<std::string, llvm::Value *> namedValues_;
  std::map<Loop *, llvm::Value *> loopIndices_;
  llvm::Function *func_;

  llvm::Type *int64Ty_;
  llvm::Type *int32Ty_;
  llvm::Constant *int64Zero_;
  llvm::Constant *int32Zero_;

public:
  LLVMEmitter() : builder_(ctx_) {
    int64Ty_ = llvm::Type::getInt64Ty(ctx_);
    int64Zero_ = llvm::Constant::getNullValue(int64Ty_);

    int32Ty_ = llvm::Type::getInt32Ty(ctx_);
    int32Zero_ = llvm::Constant::getNullValue(int32Ty_);

    M_ = std::make_unique<llvm::Module>("", ctx_);
  }

  llvm::Module *getModule() const { return M_.get(); }

  llvm::Function *emitPrototype(Program *p) {
    std::vector<llvm::Type *> argListType;

    // Construct the types for the argument list.
    for (auto *arg : p->getArgs()) {
      switch (arg->getType()->getElementType()) {
      case ElemKind::Float32Ty: {
        auto *ptrTy = llvm::PointerType::get(llvm::Type::getFloatTy(ctx_), 0);
        argListType.push_back(ptrTy);
        break;
      }
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

  ///// \returns the index of the element of indices for buffer \p bufferTy.
  /// return (x * dims[1] * dims[2]) + (y * dims[2]) + z; ...
  llvm::Value *getIndexOffsetForBuffer(const std::vector<ExprHandle> &indices,
                                       const Type *bufferTy) {
    assert(bufferTy->getDims().size() == indices.size() &&
           "invalid number of indices");

    // Accumulate the whole expression.
    llvm::Value *offset = int64Zero_;
    // For each index (x, y, z ... )
    for (int i = 0; i < indices.size(); i++) {
      // Generate the expression; (x * dims[1] * dims[2]).
      llvm::Value *dimSizeVal = generate(indices[i].get());
      // For each sizeScale:
      for (int j = i + 1; j < indices.size(); j++) {
        auto dimSize = bufferTy->getDims()[j];
        auto val = llvm::APInt(64, dimSize);
        llvm::Value *dimsI = llvm::Constant::getIntegerValue(int64Ty_, val);
        dimSizeVal = builder_.CreateMul(dimSizeVal, dimsI);
      }
      offset = builder_.CreateAdd(offset, dimSizeVal);
    }
    return offset;
  }

  llvm::Value *generate(const Expr *e) {
    auto *llvmTy = getLLVMTypeForType(e->getType());

    // Handle Index expressions.
    if (auto *ii = dynamic_cast<const IndexExpr *>(e)) {
      auto *L = ii->getLoop();
      return builder_.CreateLoad(int64Ty_, loopIndices_[L], L->getName());
    }

    // Handle Constant expressions.
    if (auto *cc = dynamic_cast<const ConstantExpr *>(e)) {
      auto val = llvm::APInt(64, cc->getValue());
      return llvm::Constant::getIntegerValue(llvmTy, val);
    }

    // Handle float-constant expressions.
    if (auto *cc = dynamic_cast<const ConstantFPExpr *>(e)) {
      auto val = llvm::APInt(64, cc->getValue());
      return llvm::ConstantFP::get(llvmTy, cc->getValue());
    }

    // Handle binary expressions.
    if (auto *bin = dynamic_cast<const BinaryExpr *>(e)) {
      bool isFP = !bin->getType().isIndexTy();
      auto *LHS = generate(bin->getLHS());
      auto *RHS = generate(bin->getRHS());

      // Mul, Add, Div, Sub, Max, Min, Pow.
      switch (bin->getKind()) {
      case BinaryExpr::BinOpKind::Add:
        if (isFP)
          return builder_.CreateFAdd(LHS, RHS);
        return builder_.CreateAdd(LHS, RHS);

      case BinaryExpr::BinOpKind::Mul:
        if (isFP)
          return builder_.CreateFMul(LHS, RHS);
        return builder_.CreateMul(LHS, RHS);

      case BinaryExpr::BinOpKind::Sub:
        if (isFP)
          return builder_.CreateFSub(LHS, RHS);
        return builder_.CreateSub(LHS, RHS);

      case BinaryExpr::BinOpKind::Div:
        if (isFP)
          return builder_.CreateFDiv(LHS, RHS);
        return builder_.CreateSDiv(LHS, RHS);
      default:
        assert(false && "Invalid operation");
      }
    }

    // Handle broadcast expressions.
    if (auto *bb = dynamic_cast<const BroadcastExpr *>(e)) {
      auto *val = generate(bb->getValue());
      auto scalarTy = val->getType();
      assert(!scalarTy->isVectorTy() && "must be a scalar");
      int width = bb->getType().getWidth();
      return builder_.CreateVectorSplat(width, val);
    }

    // Handle load-local expressions.
    if (auto *r = dynamic_cast<const LoadLocalExpr *>(e)) {
      auto *alloca = namedValues_[r->getDest()->getName()];
      auto *allocaTy =
          llvm::cast<llvm::PointerType>(alloca->getType())->getElementType();
      return builder_.CreateLoad(allocaTy, alloca, r->getDest()->getName());
    }

    // Handle Load expressions.
    if (auto *ld = dynamic_cast<const LoadExpr *>(e)) {

      auto *bufferTy = ld->getDest()->getType();
      llvm::Value *offset = getIndexOffsetForBuffer(ld->getIndices(), bufferTy);
      auto *arg = namedValues_[ld->getDest()->getName()];

      if (ld->getType().isVector()) {
        auto width = ld->getType().getWidth();
        auto *elemTy = llvm::cast<llvm::PointerType>(arg->getType())->getElementType();
        auto *vecTy = llvm::VectorType::get(elemTy, width);
        auto *ptr = builder_.CreateGEP(elemTy, arg, offset);
        return builder_.CreateLoad(vecTy, ptr, "ld");
      }

      auto *ptr = builder_.CreateGEP(arg, offset);
      return builder_.CreateLoad(ptr, "ld");
    }

    auto *ty = getLLVMTypeForType(e->getType());
    return llvm::Constant::getNullValue(ty);
    return nullptr;
  }

  void emit(StoreLocalStmt *SL) {
    auto *varAlloca = namedValues_[SL->getDest()->getName()];
    builder_.CreateStore(generate(SL->getValue()), varAlloca);
  }

  void emit(StoreStmt *SS) {
    auto *storedValue = generate(SS->getValue());
    auto *bufferTy = SS->getDest()->getType();
    llvm::Value *offset = getIndexOffsetForBuffer(SS->getIndices(), bufferTy);
    auto *arg = namedValues_[SS->getDest()->getName()];
    auto *ptr = builder_.CreateGEP(arg, offset);
    builder_.CreateStore(storedValue, ptr);
  }

  void emit(IfRange *IR) {
    llvm::Value *indexVal = generate(IR->getIndex());
    auto range = IR->getRange();

    llvm::BasicBlock *inrng = llvm::BasicBlock::Create(ctx_, "inRange", func_);
    llvm::BasicBlock *cont = llvm::BasicBlock::Create(ctx_, "continue", func_);

    auto *a = builder_.CreateICmp(llvm::CmpInst::Predicate::ICMP_SGE, indexVal,
                        llvm::ConstantInt::get(int64Ty_, range.first));
    auto *b = builder_.CreateICmp(llvm::CmpInst::Predicate::ICMP_SLT, indexVal,
                                  llvm::ConstantInt::get(int64Ty_, range.second));

    auto *orr = builder_.CreateOr(a, b);

    builder_.CreateCondBr(orr, inrng, cont);

    builder_.SetInsertPoint(inrng);
    for (auto &s : IR->getBody()) {
      emit(s);
    }
    builder_.CreateBr(cont);
    builder_.SetInsertPoint(cont);
  }

  void emit(Loop *L) {
    auto *index = builder_.CreateAlloca(int64Ty_, 0, L->getName());
    builder_.CreateStore(int64Zero_, index);

    // Record the loop index for expressions that need to reference it.
    loopIndices_[L] = index;

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
    if (auto *L = dynamic_cast<Loop *>(S)) {
      return emit(L);
    }

    if (auto *IR = dynamic_cast<IfRange *>(S)) {
      return emit(IR);
    }
    if (auto *SLS = dynamic_cast<StoreLocalStmt *>(S)) {
      return emit(SLS);
    }
    if (auto *ST = dynamic_cast<StoreStmt *>(S)) {
      return emit(ST);
    }
    assert(false);
  }

  llvm::Type *getLLVMTypeForType(const ExprType &p) {
    llvm::Type *res = nullptr;
    switch (p.getElementType()) {
    case ElemKind::Float32Ty:
      res = llvm::Type::getFloatTy(ctx_);
      break;
    case ElemKind::IndexTy:
      res = int64Ty_;
      break;

    default:
      assert(false && "Invalid type");
    }

    if (p.isVector()) {
      res = llvm::VectorType::get(res, p.getWidth());
    }

    return res;
  }

  llvm::Function *emitBenchmark(Program *p) {
    std::vector<llvm::Type *> argListType;
    argListType.push_back(llvm::Type::getInt8PtrTy(ctx_));

    // Make the function type:  void benchmark(char *mem).
    llvm::FunctionType *FT = llvm::FunctionType::get(
        llvm::Type::getVoidTy(ctx_), argListType, false);
    llvm::Function *F = llvm::Function::Create(
        FT, llvm::Function::ExternalLinkage, "benchmark", M_.get());

    // Create a new basic block to start insertion into.
    llvm::BasicBlock *BB = llvm::BasicBlock::Create(ctx_, "entry", F);
    builder_.SetInsertPoint(BB);

    // Get the input buffer.
    auto *memBuffer = F->args().begin();
    std::vector<llvm::Value *> params;

    // Construct the offsets into the memory buffer.
    uint64_t offset = 0;
    for (auto *arg : p->getArgs()) {
      switch (arg->getType()->getElementType()) {
      case ElemKind::Float32Ty: {
        llvm::Value *offsetV = llvm::ConstantInt::get(int64Ty_, offset);
        auto *gep = builder_.CreateGEP(memBuffer, offsetV);
        auto *bc =
            builder_.CreateBitCast(gep, llvm::PointerType::getFloatPtrTy(ctx_));
        params.push_back(bc);
        // Adjust the pointer for the next buffer.
        offset += arg->getType()->size() * 4;
        break;
      }
      default:
        assert(false && "Invalid parameter");
      }
    }

    builder_.CreateCall(func_, params);
    builder_.CreateRetVoid();
    llvm::verifyFunction(*F);
    F->print(llvm::outs());
    return F;
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

    for (auto *var : p->getVars()) {
      auto ty = getLLVMTypeForType(var->getType());
      namedValues_[var->getName()] =
          builder_.CreateAlloca(ty, 0, var->getName());
    }

    for (auto &stmt : p->getBody()) {
      emit(stmt);
    }

    builder_.CreateRetVoid();
    llvm::verifyFunction(*func_);
    func_->getParent()->print(llvm::outs(), 0);
    return func_;
  }
};

std::string LLVMBackend::emitProgramCode(Program *p) { return ""; }

std::string LLVMBackend::emitBenchmarkCode(Program *p, unsigned iter) {
  return "";
}

double LLVMBackend::evaluateCode(Program *p, unsigned iter) {
  LLVMEmitter EE;
  auto *func = EE.emit(p);
  EE.emitBenchmark(p);

  run(func->getParent());
  return 0;
}