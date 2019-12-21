#include "bistra/Program/Program.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Program/Utils.h"

#include <algorithm>
#include <iostream>

using namespace bistra;

/// \returns True if \p name is a legal variable name.
static bool isLegalName(const std::string &name) {
  // Check that the name is a legal C name.
  for (char c : name) {
    if (!isalnum(c) && c != '_')
      return false;
  }
  return true;
}

/// Prints \p t spaces to indent the program.
static void spaces(unsigned t) {
  for (unsigned i = 0; i < t; i++) {
    std::cout << " ";
  }
}

Program *ASTNode::getProgram() const {
  ASTNode *parent = getParent();
  assert(parent && "The node is unowned by a program");

  if (Program *p = dynamic_cast<Program *>(parent))
    return p;

  return parent->getProgram();
}

void Argument::dump() const {
  std::cout << name_ << ":";
  type_.dump();
}

void LocalVar::dump() const {
  std::cout << name_ << " : " << type_.getTypename();
}

Program::Program(const std::string &name, DebugLoc loc)
    : Scope(loc), name_(name) {}

Program::Program(const std::string &name, const std::vector<Stmt *> &body,
                 const std::vector<Argument *> &args,
                 const std::vector<LocalVar *> &vars, DebugLoc loc)
    : Scope(body, loc), name_(name), args_(args), vars_(vars) {}

Program::~Program() {
  for (auto *arg : args_) {
    delete arg;
  }
  for (auto *var : vars_) {
    delete var;
  }
}

LocalVar *Program::getVar(const std::string &name) {
  for (auto *v : vars_) {
    if (name == v->getName())
      return v;
  }
  return nullptr;
}

Argument *Program::addArgument(const std::string &name,
                               const std::vector<unsigned> &dims,
                               const std::vector<std::string> &names,
                               ElemKind Ty) {
  Type t(Ty, dims, names);
  Argument *arg = new Argument(name, t);
  addArgument(arg);
  return arg;
}

LocalVar *Program::addLocalVar(const std::string &name, ExprType Ty) {
  LocalVar *var = new LocalVar(name, Ty);
  addVar(var);
  return var;
}

LocalVar *Program::addTempVar(const std::string &nameHint, ExprType Ty) {
  unsigned counter = 1;
  std::string name = nameHint;

  // Generate the pattern: "foo14"
  do {
    name = nameHint + std::to_string(counter++);
  } while (getVar(name));

  return addLocalVar(name, Ty);
}

void Program::addArgument(Argument *arg) { args_.push_back(arg); }

void Program::addVar(LocalVar *var) { vars_.push_back(var); }

void Program::dump(unsigned indent) const {
  std::cout << "func " << getName() << "(";
  for (int i = 0, e = args_.size(); i < e; i++) {
    if (i != 0) {
      std::cout << ", ";
    }
    args_[i]->dump();
  }
  std::cout << ") {\n";

  for (auto *var : vars_) {
    std::cout << "var ";
    var->dump();
    std::cout << "\n";
  }

  Scope::dump(1);
  std::cout << "}\n";
}

uint64_t Type::hash() const {
  uint64_t h = hashJoin(getNumDims(), (uint64_t)elementType_);
  for (auto &name : getNames()) {
    h = hashJoin(h, hashString(name));
  }
  for (auto &dim : getDims()) {
    h = hashJoin(h, dim);
  }
  return h;
}

uint64_t ExprType::hash() const {
  return hashJoin((uint64_t)elementType_, width_);
}

uint64_t Argument::hash() const {
  return hashJoin(hashString(getName()), getType()->hash());
}

uint64_t LocalVar::hash() const {
  return hashJoin(hashString(getName()), type_.hash());
}

uint64_t Program::hash() const {
  // Hash the name, args and local variables.
  uint64_t hash = hashString(getName());
  for (auto &arg : args_) {
    hash = hashJoin(hash, arg->hash());
  }
  for (auto &var : vars_) {
    hash = hashJoin(hash, var->hash());
  }

  // Hash the body of the program.
  return hashJoin(Scope::hash(), hash);
}

bool Program::compare(const Stmt *other) const {
  auto *p = dynamic_cast<const Program *>(other);
  if (!p)
    return false;
  if (p->name_ != name_)
    return false;
  if (p->getArgs() != getArgs())
    return false;
  if (p->getVars() != getVars())
    return false;
  return Scope::compare(other);
}

bool BinaryExpr::isCommutative() const {
  switch (kind_) {
  case bistra::BinaryExpr::Mul:
  case bistra::BinaryExpr::Add:
  case bistra::BinaryExpr::Max:
  case bistra::BinaryExpr::Min:
    return true;

  case bistra::BinaryExpr::Div:
  case bistra::BinaryExpr::Sub:
  case bistra::BinaryExpr::Pow:
    return false;
  }
}

const char *BinaryExpr::getOpSymbol(BinOpKind kind_) {
#define CASE(opname, symbol)                                                   \
  case BinaryExpr::BinOpKind::opname: {                                        \
    return symbol;                                                             \
  }
  switch (kind_) {
    CASE(Add, " + ")
    CASE(Mul, " * ")
    CASE(Sub, " - ")
    CASE(Div, " / ")

    CASE(Max, "max")
    CASE(Min, "min")
    CASE(Pow, "pow")
  }
#undef CASE
}

const char *UnaryExpr::getOpSymbol(UnaryOpKind kind_) {
#define CASE(opname, symbol)                                                   \
  case UnaryExpr::UnaryOpKind::opname: {                                       \
    return symbol;                                                             \
  }
  switch (kind_) {
    CASE(Exp, "exp")
    CASE(Sqrt, "sqrt")
    CASE(Log, "log")
    CASE(Abs, "abs")
  }
#undef CASE
}

const char *BinaryExpr::getOpSymbol() const { return getOpSymbol(getKind()); }

const char *UnaryExpr::getOpSymbol() const { return getOpSymbol(getKind()); }

void Scope::dump(unsigned indent) const {
  for (auto &SH : body_) {
    SH->dump(indent);
  }
}

uint64_t Scope::hash() const {
  uint64_t hash = body_.size();
  for (auto &SH : body_) {
    hash = hashJoin(hash, SH->hash());
  }
  return hash;
}

bool Scope::compare(const Stmt *other) const {
  // Compare the body of the scope.
  auto *s = dynamic_cast<const Scope *>(other);
  if (!s)
    return false;
  auto &B = s->getBody();
  if (B.size() != body_.size())
    return false;
  // Compare all of the statements.
  for (int i = 0, e = B.size(); i < e; i++) {
    if (!body_[i]->compare(B[i].get()))
      return false;
  }
  return true;
}

void Scope::clear() {
  for (auto &SH : body_) {
    // Reset to unregister the handle.
    SH.setReference(nullptr);
  }
  body_.clear();
}

void Scope::takeContent(Scope *other) {
  assert(this != other && "Taking from self");
  for (auto &SH : other->body_) {
    // Reset to unregister the handle.
    auto *stms = SH.get();
    SH.setReference(nullptr);
    body_.emplace_back(stms, this);
  }
  other->clear();
}

void Scope::addStmt(Stmt *s) { body_.emplace_back(s, this); }

void Scope::removeStmt(Stmt *s) {
  // Reset to unregister the handle.
  for (auto &SH : body_) {
    if (SH.get() == s) {
      SH.setReference(nullptr);
    }
  }

  body_.erase(
      std::remove_if(body_.begin(), body_.end(),
                     [&](StmtHandle &SH) { return SH.get() == nullptr; }),
      body_.end());
}

void Scope::replaceStmt(Stmt *newS, Stmt *oldS) {
  assert(oldS->getParent() == this && "Old stmt not in this scope");
  for (auto &sth : getBody()) {
    if (sth.get() != oldS)
      continue;

    sth.setReference(newS);
    delete oldS;
  }
  assert(newS->getParent() == this && "New stmt not in this scope");
}

void Scope::insertBeforeStmt(Stmt *s, Stmt *where) {
  auto iter = std::find(body_.begin(), body_.end(), where);
  assert(iter != body_.end() && "Can't find the insertion point");
  body_.emplace(iter, s, this);
}

void Scope::insertAfterStmt(Stmt *s, Stmt *where) {
  auto iter = std::find(body_.begin(), body_.end(), where);
  assert(iter != body_.end() && "Can't find the insertion point");
  iter++;
  body_.emplace(iter, s, this);
}

uint64_t Loop::hash() const {
  // Hash the name, stride, range.
  uint64_t hash = hashString(getName());
  hash = hashJoin(hash, getEnd(), getStride());
  // Hash the body:
  return hashJoin(hash, Scope::hash());
}

bool Loop::compare(const Stmt *other) const {
  // Compare the members:
  auto *s = dynamic_cast<const Loop *>(other);
  if (!s)
    return false;

  if (s->getName() != getName())
    return false;
  if (s->getEnd() != getEnd())
    return false;
  if (s->getStride() != getStride())
    return false;

  // Compare the body:
  return Scope::compare(other);
}

void Loop::dump(unsigned indent) const {
  spaces(indent);
  std::string stride;
  if (stride_ != 1) {
    stride = std::string(", ") + std::to_string(stride_);
  }

  std::cout << "for"
            << " (" << indexName_ << " in 0.." << end_ << stride << ") {\n";
  Scope::dump(indent + 1);
  spaces(indent);
  std::cout << "}\n";
}

void IfRange::dump(unsigned indent) const {
  spaces(indent);
  std::cout << "if"
            << " (";
  val_->dump();
  std::cout << " in " << start_ << " .. " << end_ << ") {\n";
  Scope::dump(indent + 1);
  spaces(indent);
  std::cout << "}\n";
}

bool IfRange::compare(const Stmt *other) const {
  // Compare the members:
  auto *s = dynamic_cast<const IfRange *>(other);
  if (!s)
    return false;

  if (s->getRange() != getRange())
    return false;
  if (!val_->compare(s->val_.get()))
    return false;

  // Compare the body:
  return Scope::compare(other);
}

uint64_t IfRange::hash() const {
  // Hash the members.
  uint64_t hash = val_->hash();
  hash = hashJoin(hash, start_, end_);
  // Hash the body:
  return hashJoin(hash, Scope::hash());
}

void ConstantExpr::dump() const { std::cout << std::to_string(val_); }

void ConstantFPExpr::dump() const { std::cout << std::to_string(val_); }

bool ConstantExpr::compare(const Expr *other) const {
  auto *s = dynamic_cast<const ConstantExpr *>(other);
  if (!s)
    return false;
  return s->val_ == val_;
}

bool ConstantFPExpr::compare(const Expr *other) const {
  auto *s = dynamic_cast<const ConstantFPExpr *>(other);
  if (!s)
    return false;
  return s->val_ == val_;
}

bool ConstantStringExpr::compare(const Expr *other) const {
  auto *s = dynamic_cast<const ConstantStringExpr *>(other);
  if (!s)
    return false;
  return s->val_ == val_;
}

uint64_t ConstantExpr::hash() const { return val_; }

uint64_t ConstantFPExpr::hash() const { return static_cast<uint64_t>(val_); }

uint64_t ConstantStringExpr::hash() const { return hashString(getValue()); }

/// Unescape a c string. Translate '\\n' to '\n', etc.
static std::string escapeCString(const std::string &s) {
  std::string res;

  for (const char &c : s) {
    if (c == '\n') {
      res += "\\n";
      continue;
    }
    if (c == '\t') {
      res += "\\t";
      continue;
    }
    res += c;
  }

  return res;
}

void ConstantStringExpr::dump() const {
  std::cout << " \"" + escapeCString(val_) + "\" ";
}

void BroadcastExpr::dump() const {
  std::cout << "(";
  val_->dump();
  std::cout << ")";
}

bool BroadcastExpr::compare(const Expr *other) const {
  auto *e = dynamic_cast<const BroadcastExpr *>(other);
  if (!e)
    return false;

  if (e->vf_ != vf_)
    return false;
  return val_->compare(e->val_.get());
}

uint64_t BroadcastExpr::hash() const {
  // Hash the type, which includes the VF.
  return hashJoin(getType().hash(), getValue()->hash());
}

uint64_t LoadExpr::hash() const {
  // Hash the type, which includes the VF.
  return hashJoin(getType().hash(), getGep()->hash());
}

LoadExpr::LoadExpr(GEPExpr *gep, DebugLoc loc)
    : Expr(ElemKind::IndexTy, loc), gep_(gep, this) {
  setType(ExprType(getDest()->getType()->getElementType()));
}

LoadExpr::LoadExpr(GEPExpr *gep, ExprType elemTy, DebugLoc loc)
    : Expr(elemTy, loc), gep_(gep, this) {
      assert(dynamic_cast<GEPExpr*>(gep_.get()));
    }

LoadExpr::LoadExpr(Argument *arg, const std::vector<Expr *> &indices,
                   ExprType elemTy, DebugLoc loc)
    : LoadExpr(arg, indices, loc) {
  // Override the type that we guess in the untyped ctor.
  setType(elemTy);
}

LoadExpr::LoadExpr(Argument *arg, const std::vector<Expr *> &indices,
                   DebugLoc loc)
    : Expr(ElemKind::IndexTy, loc), gep_(new GEPExpr(arg, indices, loc), this) {

  // This loads a scalar value from the buffer.
  setType(ExprType(arg->getType()->getElementType()));
}

bool LoadExpr::compare(const Expr *other) const {
  auto *e = dynamic_cast<const LoadExpr *>(other);
  if (!e)
    return false;

  if (!e->getType().isEqual(getType()))
    return false;

  return gep_->compare(e->gep_.get());
}

void GEPExpr::dump() const {
  std::cout << getDest()->getName() << "[";
  bool first = true;
  for (auto &I : getIndices()) {
    if (!first) {
      std::cout << ",";
    }
    first = false;
    I->dump();
  }
  std::cout << "]";
}

uint64_t GEPExpr::hash() const {
  uint64_t hash = arg_->hash();
  for (auto &I : indices_) {
    hash = hashJoin(hash, I->hash());
  }
  return hash;
}

bool GEPExpr::compare(const Expr *other) const {
  auto *e = dynamic_cast<const GEPExpr *>(other);
  if (!e)
    return false;

  if (e->getDest() != getDest())
    return false;

  auto &B = e->getIndices();
  if (B.size() != indices_.size())
    return false;

  // Compare all of the indices.
  for (int i = 0, e = B.size(); i < e; i++) {
    if (!indices_[i]->compare(B[i].get()))
      return false;
  }

  return true;
}

void LoadExpr::dump() const {
  gep_->dump();
  if (getType().isVector()) {
    std::cout << "." << getType().getWidth();
  }
}

void LoadLocalExpr::dump() const { std::cout << var_->getName(); }

bool LoadLocalExpr::compare(const Expr *other) const {
  auto *e = dynamic_cast<const LoadLocalExpr *>(other);
  if (!e)
    return false;

  // Compare the type and address.
  if (!e->getType().isEqual(getType()))
    return false;
  return e->getDest() == getDest();
}

uint64_t LoadLocalExpr::hash() const {
  return hashJoin(getType().hash(), var_->hash());
}

StoreStmt::StoreStmt(GEPExpr *gep, Expr *value, bool accumulate, DebugLoc loc)
    : Stmt(loc), gep_(gep, this), value_(value, this), accumulate_(accumulate) {
      assert(dynamic_cast<GEPExpr*>(gep_.get()));
}

StoreStmt::StoreStmt(Argument *arg, const std::vector<Expr *> &indices,
                     Expr *value, bool accumulate, DebugLoc loc)
    : Stmt(loc), gep_(new GEPExpr(arg, indices, loc), this),
      value_(value, this), accumulate_(accumulate) {
        assert(dynamic_cast<GEPExpr*>(gep_.get()));
      }

      bool StoreStmt::compare(const Stmt *other) const {
        auto *e = dynamic_cast<const StoreStmt *>(other);
        if (!e)
          return false;

        return e->accumulate_ == accumulate_ && gep_->compare(e->gep_.get()) &&
               value_->compare(e->value_.get());
      }

      uint64_t StoreStmt::hash() const {
        return hashJoin(accumulate_, gep_->hash(), value_->hash());
      }

std::vector<Expr *> StoreStmt::cloneIndicesPtr(CloneCtx &map) {
  std::vector<Expr *> ret;
  // Clone and save the raw unowned pointers.
  for (auto &h : getIndices()) {
    ret.push_back(h.get()->clone(map));
  }
  return ret;
}

std::vector<Expr *> CallStmt::cloneIndicesPtr(CloneCtx &map) {
  std::vector<Expr *> ret;
  // Clone and save the raw unowned pointers.
  for (auto &h : getParams()) {
    ret.push_back(h.get()->clone(map));
  }
  return ret;
}

uint64_t CallStmt::hash() const {
  uint64_t hash = hashString(name_);
  for (auto &p : params_) {
    hash = hashJoin(hash, p->hash());
  }
  return hashJoin(hash, params_.size());
}

void CallStmt::dump(unsigned indent) const {
  spaces(indent);
  std::cout << getName() << "(";
  bool first = true;
  for (auto &I : params_) {
    if (!first) {
      std::cout << ",";
    }
    first = false;
    I->dump();
  }
  std::cout << ")";
  std::cout << ";\n";
}

bool CallStmt::compare(const Stmt *other) const {
  auto *e = dynamic_cast<const CallStmt *>(other);
  if (!e)
    return false;

  if (e->getName() != getName())
    return false;

  auto &B = e->getParams();
  if (B.size() != params_.size())
    return false;

  // Compare all of the indices.
  for (int i = 0, e = B.size(); i < e; i++) {
    if (!params_[i]->compare(B[i].get()))
      return false;
  }

  return true;
}

void StoreStmt::dump(unsigned indent) const {
  spaces(indent);
  gep_->dump();
  if (value_->getType().isVector()) {
    std::cout << "." << value_->getType().getWidth();
  }
  std::cout << (accumulate_ ? " += " : " = ");
  value_->dump();
  std::cout << ";\n";
}

uint64_t StoreLocalStmt::hash() const {
  return hashJoin(accumulate_, var_->hash(), value_->hash());
}

bool StoreLocalStmt::compare(const Stmt *other) const {
  auto *e = dynamic_cast<const StoreLocalStmt *>(other);
  if (!e)
    return false;

  if (e->accumulate_ != accumulate_)
    return false;
  if (e->var_ != var_)
    return false;
  return e->value_->compare(value_.get());
}

void StoreLocalStmt::dump(unsigned indent) const {
  spaces(indent);
  std::cout << var_->getName();
  std::cout << (accumulate_ ? " += " : " = ");
  value_->dump();
  std::cout << ";\n";
}

void IndexExpr::dump() const { std::cout << loop_->getName(); }

bool IndexExpr::compare(const Expr *other) const {
  auto *e = dynamic_cast<const IndexExpr *>(other);
  if (!e)
    return false;
  return e->getLoop() == getLoop();
}

uint64_t IndexExpr::hash() const {
  // We need to break a cycle here. Just use the name and some random number.
  return hashJoin(hashString(getLoop()->getName()), 0xff);
}

bool BinaryExpr::compare(const Expr *other) const {
  auto *e = dynamic_cast<const BinaryExpr *>(other);
  if (!e)
    return false;
  return e->getKind() == getKind() && LHS_->compare(e->LHS_.get()) &&
         RHS_->compare(e->RHS_.get());
}

uint64_t BinaryExpr::hash() const {
  return hashJoin((uint64_t)kind_, getLHS()->hash(), getRHS()->hash());
}

void BinaryExpr::dump() const {
  std::cout << "(";
  switch (getKind()) {
  case Mul:
  case Add:
  case Div:
  case Sub: {
    LHS_->dump();
    std::cout << " " << getOpSymbol() << " ";
    RHS_->dump();
    break;
  }
  case Max:
  case Min:
  case Pow:
    std::cout << " " << getOpSymbol() << "(";
    LHS_->dump();
    std::cout << ", ";
    RHS_->dump();
    std::cout << ")";
    break;
  }
  std::cout << ")";
}

void UnaryExpr::dump() const {
  switch (getKind()) {
  case Exp:
  case Sqrt:
  case Log:
  case Abs:
    std::cout << " " << getOpSymbol() << "(";
    val_->dump();
    std::cout << ")";
    break;
  }
}
uint64_t UnaryExpr::hash() const {
  return hashJoin((uint64_t)kind_, val_->hash());
}

bool UnaryExpr::compare(const Expr *other) const {
  auto *e = dynamic_cast<const UnaryExpr *>(other);
  if (!e)
    return false;
  return e->getKind() == getKind() && val_->compare(e->val_.get());
}

Expr *ConstantExpr::clone(CloneCtx &map) { return new ConstantExpr(val_); }

Expr *ConstantFPExpr::clone(CloneCtx &map) { return new ConstantFPExpr(val_); }

Expr *ConstantStringExpr::clone(CloneCtx &map) {
  return new ConstantStringExpr(val_);
}

Expr *BinaryExpr::clone(CloneCtx &map) {
  return new BinaryExpr(LHS_->clone(map), RHS_->clone(map), getKind(),
                        getLoc());
}

Expr *UnaryExpr::clone(CloneCtx &map) {
  return new UnaryExpr(val_->clone(map), getKind(), getLoc());
}

Expr *BroadcastExpr::clone(CloneCtx &map) {
  return new BroadcastExpr(val_->clone(map), vf_);
}

Expr *GEPExpr::clone(CloneCtx &map) {
  Argument *arg = map.get(arg_);
  std::vector<Expr *> indices;
  for (auto &E : indices_) {
    indices.push_back(E->clone(map));
  }

  return new GEPExpr(arg, indices, getLoc());
}

Expr *LoadExpr::clone(CloneCtx &map) {
  return new LoadExpr((GEPExpr *)gep_->clone(map), getType(), getLoc());
}

Expr *LoadLocalExpr::clone(CloneCtx &map) {
  LocalVar *var = map.get(var_);
  verify();
  return new LoadLocalExpr(var, getLoc());
}

Stmt *CallStmt::clone(CloneCtx &map) {
  verify();
  std::vector<Expr *> indices = cloneIndicesPtr(map);
  return new CallStmt(getName(), indices, getLoc());
}

Stmt *StoreStmt::clone(CloneCtx &map) {
  verify();
  return new StoreStmt((GEPExpr *)gep_->clone(map), value_->clone(map),
                       accumulate_, getLoc());
}

Stmt *StoreLocalStmt::clone(CloneCtx &map) {
  LocalVar *var = map.get(var_);
  verify();
  return new StoreLocalStmt(var, value_->clone(map), accumulate_, getLoc());
}

Expr *IndexExpr::clone(CloneCtx &map) {
  Loop *loop = map.get(this->loop_);
  return new IndexExpr(loop);
}

Stmt *Loop::clone(CloneCtx &map) {
  Loop *loop = new Loop(indexName_, getLoc(), end_, stride_);
  map.map(this, loop);
  for (auto &MH : body_) {
    loop->addStmt(MH->clone(map));
  }
  return loop;
}

Stmt *IfRange::clone(CloneCtx &map) {
  IfRange *IR = new IfRange(val_->clone(map), start_, end_, getLoc());
  for (auto &MH : body_) {
    IR->addStmt(MH->clone(map));
  }
  return IR;
}

Program *Program::clone() {
  CloneCtx ctx;

  return (Program *)clone(ctx);
}

Stmt *Program::clone(CloneCtx &map) {
  verify();
  Program *np = new Program(getName(), getLoc());
  std::vector<Argument *> newArgs;
  std::vector<LocalVar *> newVars;

  for (auto *arg : args_) {
    Argument *newArg = new Argument(*arg);
    np->addArgument(newArg);
    map.map(arg, newArg);
  }
  for (auto *var : vars_) {
    LocalVar *newVar = new LocalVar(*var);
    np->addVar(newVar);
    map.map(var, newVar);
  }

  for (auto &MH : body_) {
    np->addStmt(MH->clone(map));
  }
  return np;
}

void BinaryExpr::verify() const {
  assert(LHS_->getType() == RHS_->getType() && "LHS and RHS type mismatch");
  assert(LHS_.getParent() == this && "Invalid handle owner pointer");
  assert(RHS_.getParent() == this && "Invalid handle owner pointer");
  assert(LHS_.get() && "Invalid operand");
  assert(RHS_.get() && "Invalid operand");
  LHS_->verify();
  RHS_->verify();
}

void UnaryExpr::verify() const {
  assert(val_.getParent() == this && "Invalid handle owner pointer");
  assert(val_.get() && "Invalid operand");
  val_->verify();
}

void ConstantExpr::verify() const {}

void ConstantFPExpr::verify() const {}

void ConstantStringExpr::verify() const {}

void Loop::verify() const {
  assert(end_ > 0 && "Loops must not be empty");
  assert(end_ % stride_ == 0 && "Trip count must be divisible by the stride");
  assert(stride_ > 0 && stride_ < 1024 && "Invalid stride");
  assert(isLegalName(getName()) && "Invalid character in index name");
  Scope::verify();
}

void IfRange::verify() const {
  assert(end_ >= start_ && "Invalid range");
  val_->verify();
  Scope::verify();
}

void Scope::verify() const {
  for (auto &EH : body_) {
    assert(EH.get() && "Invalid operand");
    EH.verify();
    EH->verify();
  }
}

void IndexExpr::verify() const {
  assert(getType().isIndexTy() && "Invalid index type");
  const ASTNode *parent = this;
  ASTNode *loop = getLoop();
  while (loop != parent) {
    parent = parent->getParent();
    assert(parent && "Reached the top of the program without finding the loop. "
                     "This means that the index is not contained within it's "
                     "loop scope.");
  }
}
void BroadcastExpr::verify() const {
  val_->verify();
  assert(getType().getWidth() == vf_ && "Invalid vectorization factor");
  assert(val_->getType().getWidth() == 1 && "Broadcasting a vector");
}

void GEPExpr::verify() const {
  for (auto &E : indices_) {
    E.verify();
    E->verify();
    assert(E.getParent() == this && "Invalid handle owner pointer");
    assert(E->getType().isIndexTy() && "Argument must be of index kind");
  }
  assert(indices_.size() && "Empty argument list");
  assert(arg_->getType()->getNumDims() == indices_.size() &&
         "Invalid number of indices");

  assert(getType().getElementType() == ElemKind::PtrTy);
}

void LoadExpr::verify() const {
  assert(dynamic_cast<GEPExpr*>(gep_.get()));
  gep_->verify();
  // Check the store element kind.
  ElemKind EK = getDest()->getType()->getElementType();
  assert(getType().getElementType() == EK && "Loaded element type mismatch");
}

void LoadLocalExpr::verify() const {
  Program *prog = getProgram();
  assert(prog->getVars().size() && "Program has no locals!");
  assert(prog->getVar(var_->getName()) == var_ && "Vars are not uniqued");
  assert(getType() == var_->getType() && "Loaded element type mismatch");
}

void StoreStmt::verify() const {
  assert(dynamic_cast<GEPExpr*>(gep_.get()));
  gep_->verify();
  gep_.verify();
  assert(value_.getParent() == this && "Invalid handle owner pointer");
  auto storedType = value_->getType();

  value_->verify();
  value_.verify();

  // Check the store value type.
  ElemKind EK = getDest()->getType()->getElementType();
  assert(storedType.getElementType() == EK && "Stored element type mismatch");
}

void CallStmt::verify() const {
  for (auto &E : params_) {
    E.verify();
    E->verify();
    assert(E.getParent() == this && "Invalid handle owner pointer");
    assert(E->getType().isIndexTy() && "Argument must be of index kind");
  }
}

void StoreLocalStmt::verify() const {
  Program *prog = getProgram();
  assert(prog->getVars().size() && "Program has no locals!");
  assert(prog->getVar(var_->getName()) == var_ && "Vars are not uniqued");

  assert(value_.getParent() == this && "Invalid handle owner pointer");
  value_->verify();
  value_.verify();
  assert(value_->getType() == var_->getType() && "invalid stored type");
}

void Argument::verify() const {
  assert(isLegalName(getName()) && "Invalid character in argument name");
}

void LocalVar::verify() const {
  assert(isLegalName(getName()) && "Invalid character in argument name");
}

void Program::verify() const {
  for (auto *a : args_) {
    a->verify();
  }
  for (auto *a : vars_) {
    a->verify();
  }
  assert(isLegalName(getName()) && "Invalid program name.");
  Scope::verify();
}

void Program::visit(NodeVisitor *visitor) { Scope::visit(visitor); }

void BinaryExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  LHS_->visit(visitor);
  RHS_->visit(visitor);
  visitor->leave(this);
}

void UnaryExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  val_->visit(visitor);
  visitor->leave(this);
}

void ConstantExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  visitor->leave(this);
}

void ConstantFPExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  visitor->leave(this);
}

void ConstantStringExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  visitor->leave(this);
}

void IndexExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  visitor->leave(this);
}

void Scope::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  for (auto &sh : body_) {
    sh->visit(visitor);
  }
  visitor->leave(this);
}

void Loop::visit(NodeVisitor *visitor) { Scope::visit(visitor); }

void IfRange::visit(NodeVisitor *visitor) {
  val_->visit(visitor);
  Scope::visit(visitor);
}

void StoreStmt::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  gep_->visit(visitor);
  value_->visit(visitor);
  visitor->leave(this);
}

void CallStmt::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  for (auto &ii : this->getParams()) {
    ii.get()->visit(visitor);
  }
  visitor->leave(this);
}

void StoreLocalStmt::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  value_->visit(visitor);
  visitor->leave(this);
}

void BroadcastExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  val_->visit(visitor);
  visitor->leave(this);
}

void GEPExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  for (auto &ii : this->getIndices()) {
    ii.get()->visit(visitor);
  }
  visitor->leave(this);
}

void LoadExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  gep_->visit(visitor);
  visitor->leave(this);
}

void LoadLocalExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  visitor->leave(this);
}

void Expr::replaceUseWith(Expr *other) {
  user_->setReference(other);
  delete this;
}

ASTNode *Expr::getParent() const {
  auto *owner = getOwnerHandle();
  if (!owner)
    return nullptr;
  return owner->getParent();
}

ASTNode *Stmt::getParent() const {
  auto *owner = getOwnerHandle();
  if (!owner)
    return nullptr;
  return owner->getParent();
}
