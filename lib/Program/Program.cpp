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

Program::Program() : body_(new Scope()) {}

Program::Program(Scope *body, const std::vector<Argument *> &args)
    : args_(args), body_(body) {}

Program::~Program() {
  delete body_;
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

void Program::dump() {
  std::cout << "Program (";
  for (int i = 0, e = args_.size(); i < e; i++) {
    if (i != 0) {
      std::cout << ", ";
    }
    args_[i]->dump();
  }
  std::cout << ") {\n";
  body_->dump(1);
  std::cout << "}\n";
}

void Scope::dump(unsigned indent) const {
  for (auto *s : body_) {
    s->dump(indent);
  }
}

void Loop::dump(unsigned indent) const {
  spaces(indent);
  std::cout << "for (" << indexName << " in 0.." << end_ << ", VF=" << vf_
            << ") {\n";
  body_->dump(indent + 1);
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
  loop->addStmt(body_->clone(map));
  return loop;
}

Stmt *Scope::clone(CloneCtx &map) {
  Scope *s = new Scope();
  for (auto *m : body_) {
    s->addStmt(m->clone(map));
  }
  return s;
}

Program *Program::clone() {
  CloneCtx ctx;
  return clone(ctx);
}

Program *Program::clone(CloneCtx &map) {
  verify();
  std::vector<Argument *> newArgs;
  for (auto *arg : args_) {
    Argument *newArg = new Argument(*arg);
    newArgs.push_back(newArg);
    map.map(arg, newArg);
  }
  return new Program((Scope *)body_->clone(map), newArgs);
}

void BinaryExpr::verify() const {
  assert(LHS_->getType() == RHS_->getType() && "LHS and RHS type mismatch");
}

void ConstantExpr::verify() const {}

void ConstantFPExpr::verify() const {}

void Loop::verify() const {
  body_->verify();
  assert(end_ > 0 && "Loops must not be empty");
  assert(end_ % vf_ == 0 &&
         "Trip count must be divisible by vectorization factor");
  assert(vf_ > 0 && vf_ < 64 && "Invalid vectorization factor");
  assert(isLegalName(getName()) && "Invalid character in index name");
}

void Scope::verify() const {
  for (auto *E : body_) {
    E->verify();
  }
}

void IndexExpr::verify() const {
  assert(getType().isIndexTy() && "Invalid index type");
}

void LoadExpr::verify() const {
  for (auto &E : indices_) {
    E.verify();
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
    assert(E->getType().isIndexTy() && "Argument must be of index kind");
  }
  assert(indices_.size() && "Empty argument list");
  assert(arg_->getType()->getNumDims() == indices_.size() &&
         "Invalid number of indices");

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

void Program::verify() {
  body_->verify();

  // Verify the arguments.
  for (auto *a : args_) {
    a->verify();
  }
}

void Program::visit(NodeVisitor *visitor) { body_->visit(visitor); }

void BinaryExpr::visit(NodeVisitor *visitor) {
  visitor->handle(this);
  LHS_->visit(visitor);
  RHS_->visit(visitor);
}

void ConstantExpr::visit(NodeVisitor *visitor) { visitor->handle(this); }

void ConstantFPExpr::visit(NodeVisitor *visitor) { visitor->handle(this); }

void IndexExpr::visit(NodeVisitor *visitor) { visitor->handle(this); }

void Scope::visit(NodeVisitor *visitor) {
  visitor->handle(this);
  for (auto *s : body_) {
    s->visit(visitor);
  }
}

void Loop::visit(NodeVisitor *visitor) {
  visitor->handle(this);
  body_->visit(visitor);
}

void StoreStmt::visit(NodeVisitor *visitor) {
  visitor->handle(this);
  for (auto &ii : this->getIndices()) {
    ii.get()->visit(visitor);
  }
  value_->visit(visitor);
}

void LoadExpr::visit(NodeVisitor *visitor) {
  visitor->handle(this);
  for (auto &ii : this->getIndices()) {
    ii.get()->visit(visitor);
  }
}

void Expr::replaceUserWith(Expr *other) {
  user_->set(other);
}
