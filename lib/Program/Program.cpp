#include "bistra/Program/Program.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Program/Utils.h"

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
  std::cout << "def " << getName() << "(";
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

void Loop::dump(unsigned indent) const {
  spaces(indent);
  std::string vf;
  if (stride_ != 1) {
    vf = std::string(".") + std::to_string(stride_);
  }

  std::cout << "for" << vf << " (" << indexName_ << " in 0.." << end_
            << ") {\n";
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

void ConstantExpr::dump() const {
  std::cout << " " + std::to_string(val_) + " ";
}

void ConstantFPExpr::dump() const {
  std::cout << " " + std::to_string(val_) + " ";
}

void BroadcastExpr::dump() const {
  std::cout << "(";
  val_->dump();
  std::cout << ")";
}

void LoadExpr::dump() const {
  std::cout << arg_->getName() << "[";
  bool first = true;
  for (auto &I : indices_) {
    if (!first) {
      std::cout << ",";
    }
    first = false;
    I->dump();
  }
  std::cout << "]";
  if (getType().isVector()) {
    std::cout << "." << getType().getWidth();
  }
}

void LoadLocalExpr::dump() const { std::cout << var_->getName(); }

std::vector<Expr *> StoreStmt::cloneIndicesPtr(CloneCtx &map) {
  std::vector<Expr *> ret;
  // Clone and save the raw unowned pointers.
  for (auto &h : getIndices()) {
    ret.push_back(h.get()->clone(map));
  }
  return ret;
}

void StoreStmt::dump(unsigned indent) const {
  spaces(indent);
  std::cout << arg_->getName() << "[";
  bool first = true;
  for (auto &I : indices_) {
    if (!first) {
      std::cout << ",";
    }
    first = false;
    I->dump();
  }
  std::cout << "]";
  if (value_->getType().isVector()) {
    std::cout << "." << value_->getType().getWidth();
  }
  std::cout << (accumulate_ ? " += " : " = ");
  value_->dump();
  std::cout << ";\n";
}

void StoreLocalStmt::dump(unsigned indent) const {
  spaces(indent);
  std::cout << var_->getName();
  std::cout << (accumulate_ ? " += " : " = ");
  value_->dump();
  std::cout << ";\n";
}

void IndexExpr::dump() const { std::cout << loop_->getName(); }

void BinaryExpr::dump() const {
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

Expr *ConstantExpr::clone(CloneCtx &map) {
  return new ConstantExpr(this->val_);
}

Expr *ConstantFPExpr::clone(CloneCtx &map) {
  return new ConstantFPExpr(this->val_);
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

Expr *LoadExpr::clone(CloneCtx &map) {
  Argument *arg = map.get(arg_);
  std::vector<Expr *> indices;
  for (auto &E : indices_) {
    indices.push_back(E->clone(map));
  }

  return new LoadExpr(arg, indices, getType(), getLoc());
}

Expr *LoadLocalExpr::clone(CloneCtx &map) {
  LocalVar *var = map.get(var_);
  verify();
  return new LoadLocalExpr(var, getLoc());
}

Stmt *StoreStmt::clone(CloneCtx &map) {
  Argument *arg = map.get(arg_);
  verify();
  std::vector<Expr *> indices = cloneIndicesPtr(map);
  return new StoreStmt(arg, indices, value_->clone(map), accumulate_, getLoc());
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

void LoadExpr::verify() const {
  for (auto &E : indices_) {
    E.verify();
    assert(E.getParent() == this && "Invalid handle owner pointer");
    assert(E->getType().isIndexTy() && "Argument must be of index kind");
  }
  assert(indices_.size() && "Empty argument list");
  assert(arg_->getType()->getNumDims() == indices_.size() &&
         "Invalid number of indices");

  // Check the store element kind.
  ElemKind EK = arg_->getType()->getElementType();
  assert(getType().getElementType() == EK && "Loaded element type mismatch");
}

void LoadLocalExpr::verify() const {
  Program *prog = getProgram();
  assert(prog->getVars().size() && "Program has no locals!");
  assert(prog->getVar(var_->getName()) == var_ && "Vars are not uniqued");
  assert(getType() == var_->getType() && "Loaded element type mismatch");
}

void StoreStmt::verify() const {
  for (auto &E : indices_) {
    E.verify();
    E->verify();
    assert(E.getParent() == this && "Invalid handle owner pointer");
    assert(E->getType().isIndexTy() && "Argument must be of index kind");
  }
  assert(indices_.size() && "Empty argument list");
  assert(arg_->getType()->getNumDims() == indices_.size() &&
         "Invalid number of indices");

  assert(value_.getParent() == this && "Invalid handle owner pointer");
  auto storedType = value_->getType();

  value_->verify();
  value_.verify();

  // Check the store value type.
  ElemKind EK = arg_->getType()->getElementType();
  assert(storedType.getElementType() == EK && "Stored element type mismatch");
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
  for (auto &ii : this->getIndices()) {
    ii.get()->visit(visitor);
  }
  value_->visit(visitor);
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

void LoadExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  for (auto &ii : this->getIndices()) {
    ii.get()->visit(visitor);
  }
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
