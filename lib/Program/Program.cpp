#include "bistra/Program/Program.h"
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

void Argument::dump() const {
  std::cout << name_ << ":";
  type_.dump();
}

Program::Program(const std::vector<Stmt *> &body,
                 const std::vector<Argument *> &args)
    : Scope(body), args_(args) {}

Program::~Program() {
  for (auto *arg : args_) {
    delete arg;
  }
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

void Program::addArgument(Argument *arg) { args_.push_back(arg); }

void Program::dump(unsigned indent) const {
  std::cout << "Program (";
  for (int i = 0, e = args_.size(); i < e; i++) {
    if (i != 0) {
      std::cout << ", ";
    }
    args_[i]->dump();
  }
  std::cout << ") {\n";
  Scope::dump(1);
  std::cout << "}\n";
}

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

  body_.erase(std::remove_if(body_.begin(), body_.end(),
                             [&](StmtHandle &SH) { return SH.get() == nullptr; }),
              body_.end());
}

void Scope::insertBeforeStmt(Stmt *s, Stmt *where) {
  auto iter = std::find(body_.begin(), body_.end(), where);
  assert(iter != body_.end() && "Can't find the insertion point");
  body_.emplace(iter, s, this);
}

void Loop::dump(unsigned indent) const {
  spaces(indent);
  std::cout << "for (" << indexName << " in 0.." << end_ << ", VF=" << vf_
            << ") {\n";
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
  std::cout << (accumulate_ ? "] += " : "] = ");
  value_->dump();
  std::cout << ";\n";
}

void IndexExpr::dump() const { std::cout << loop_->getName(); }

void AddExpr::dump() const {
  LHS_->dump();
  std::cout << " + ";
  RHS_->dump();
}

void MulExpr::dump() const {
  LHS_->dump();
  std::cout << " * ";
  RHS_->dump();
}

Expr *ConstantExpr::clone(CloneCtx &map) {
  return new ConstantExpr(this->val_);
}

Expr *ConstantFPExpr::clone(CloneCtx &map) {
  return new ConstantFPExpr(this->val_);
}

Expr *AddExpr::clone(CloneCtx &map) {
  return new AddExpr(LHS_->clone(map), RHS_->clone(map));
}
Expr *MulExpr::clone(CloneCtx &map) {
  return new AddExpr(LHS_->clone(map), RHS_->clone(map));
}

Expr *LoadExpr::clone(CloneCtx &map) {
  Argument *arg = map.get(arg_);
  std::vector<Expr *> indices;
  for (auto &E : indices_) {
    indices.push_back(E->clone(map));
  }

  return new LoadExpr(arg, indices);
}

Stmt *StoreStmt::clone(CloneCtx &map) {
  Argument *arg = map.get(arg_);
  verify();
  std::vector<Expr *> indices;
  for (auto &E : indices_) {
    indices.push_back(E->clone(map));
  }

  return new StoreStmt(arg, indices, value_->clone(map), accumulate_);
}

Expr *IndexExpr::clone(CloneCtx &map) {
  Loop *loop = map.get(this->loop_);
  return new IndexExpr(loop);
}

Stmt *Loop::clone(CloneCtx &map) {
  Loop *loop = new Loop(indexName, end_, vf_);
  map.map(this, loop);
  for (auto &MH : body_) {
    loop->addStmt(MH->clone(map));
  }
  return loop;
}

Program *Program::clone() {
  CloneCtx ctx;

  return (Program *)clone(ctx);
}

Stmt *Program::clone(CloneCtx &map) {
  verify();
  Program *np = new Program();
  std::vector<Argument *> newArgs;
  for (auto *arg : args_) {
    Argument *newArg = new Argument(*arg);
    np->addArgument(newArg);
    map.map(arg, newArg);
  };
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
}

void ConstantExpr::verify() const {}

void ConstantFPExpr::verify() const {}

void Loop::verify() const {
  assert(end_ > 0 && "Loops must not be empty");
  assert(end_ % vf_ == 0 &&
         "Trip count must be divisible by vectorization factor");
  assert(vf_ > 0 && vf_ < 64 && "Invalid vectorization factor");
  assert(isLegalName(getName()) && "Invalid character in index name");
  Scope::verify();
}

void Scope::verify() const {
  for (auto &EH : body_) {
    assert(EH.get() && "Invalid operand");
    EH->verify();
  }
}

void IndexExpr::verify() const {
  assert(getType().isIndexTy() && "Invalid index type");
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

  // Get the store element kind and vectorization factor.
  ElemKind EK = arg_->getType()->getElementType();
  auto &lastIndex = indices_[indices_.size() - 1];
  unsigned VF = lastIndex->getType().getWidth();
  assert(getType().getWidth() == VF &&
         "Loaded type does not match vectorization factor");
  assert(getType().getElementType() == EK && "Loaded element type mismatch");
}

void StoreStmt::verify() const {
  for (auto &E : indices_) {
    E.verify();
    assert(E.getParent() == this && "Invalid handle owner pointer");
    assert(E->getType().isIndexTy() && "Argument must be of index kind");
  }
  assert(indices_.size() && "Empty argument list");
  assert(arg_->getType()->getNumDims() == indices_.size() &&
         "Invalid number of indices");

  assert(value_.getParent() == this && "Invalid handle owner pointer");

  auto storedType = value_->getType();

  // Get the store element kind and vectorization factor.
  ElemKind EK = arg_->getType()->getElementType();
  auto &lastIndex = indices_[indices_.size() - 1];
  unsigned VF = lastIndex->getType().getWidth();
  assert(storedType.getWidth() == VF &&
         "Stored type does not match vectorization factor");
  assert(storedType.getElementType() == EK && "Stored element type mismatch");
}

void Argument::verify() const {
  assert(isLegalName(getName()) && "Invalid character in argument name");
}

void Program::verify() const {
  // Verify the arguments.
  for (auto *a : args_) {
    a->verify();
  }
  Scope::verify();
}

void Program::visit(NodeVisitor *visitor) { Scope::visit(visitor); }

void BinaryExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  LHS_->visit(visitor);
  RHS_->visit(visitor);
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

void StoreStmt::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  for (auto &ii : this->getIndices()) {
    ii.get()->visit(visitor);
  }
  value_->visit(visitor);
  visitor->leave(this);
}

void LoadExpr::visit(NodeVisitor *visitor) {
  visitor->enter(this);
  for (auto &ii : this->getIndices()) {
    ii.get()->visit(visitor);
  }
  visitor->leave(this);
}

void Expr::replaceUseWith(Expr *other) {
  user_->setReference(other);
  delete this;
}

ASTNode *Expr::getParent() const { return getOwnerHandle()->getParent(); }

ASTNode *Stmt::getParent() const { return getOwnerHandle()->getParent(); }
