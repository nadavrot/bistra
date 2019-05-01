#include "bistra/Program/Program.h"

#include <iostream>

using namespace bistra;

static void spaces(unsigned t) {
  for (unsigned i = 0; i < t; i++) {
    std::cout << " ";
  }
}

void Type::dump() {
  std::cout << getElementName() << "<";
  for (int i = 0, e = sizes_.size(); i < e; i++) {
    if (i != 0) {
      std::cout << ",";
    }
    std::cout << names_[i] << ":" << sizes_[i];
  }
  std::cout << ">";
}

void ExprType::dump() {
  std::cout << "<" << getWidth() << " x " << getElementName() << ">";
}

void Argument::dump() {
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

void Program::addArgument(const std::string &name,
                          const std::vector<unsigned> &dims,
                          const std::vector<std::string> &names, ElemKind Ty) {
  Type t(Ty, dims, names);
  addArgument(new Argument(name, t));
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

void Scope::dump(unsigned indent) {
  for (auto *s : body_) {
    s->dump(indent);
  }
}

void Loop::dump(unsigned indent) {
  spaces(indent);
  std::cout << "for (" << c_ << " in 0.." << end_ << ", VF=" << vf_ << ") {\n";
  body_->dump(indent + 1);
  spaces(indent);
  std::cout << "}\n";
}

void ConstantExpr::dump() { std::cout << " " + std::to_string(val_) + " "; }

void LoadExpr::dump() {
  std::cout << arg_->getName() << "[";
  bool first = true;
  for (auto *I : indices_) {
    if (!first) {
      std::cout << ",";
    }
    first = false;
    I->dump();
  }
  std::cout << "]";
}

void StoreStmt::dump(unsigned indent) {
  spaces(indent);
  std::cout << arg_->getName() << "[";
  bool first = true;
  for (auto *I : indices_) {
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

void IndexExpr::dump() { std::cout << loop_->getName(); }

void AddExpr::dump() {
  LHS_->dump();
  std::cout << " + ";
  RHS_->dump();
}

void MulExpr::dump() {
  LHS_->dump();
  std::cout << " * ";
  RHS_->dump();
}

Expr *ConstantExpr::clone(CloneCtx &map) {
  return new ConstantExpr(this->val_);
}

Expr *AddExpr::clone(CloneCtx &map) {
  return new AddExpr(LHS_->clone(map), RHS_->clone(map));
}
Expr *MulExpr::clone(CloneCtx &map) {
  return new AddExpr(LHS_->clone(map), RHS_->clone(map));
}

Expr *LoadExpr::clone(CloneCtx &map) {
  Argument *arg = map.args[arg_];
  std::vector<Expr *> indices;
  for (auto *E : indices_) {
    indices.push_back(E->clone(map));
  }

  return new LoadExpr(arg, indices);
}

Stmt *StoreStmt::clone(CloneCtx &map) {
  Argument *arg = map.args[arg_];
  std::vector<Expr *> indices;
  for (auto *E : indices_) {
    indices.push_back(E->clone(map));
  }

  return new StoreStmt(arg, indices, value_->clone(map), accumulate_);
}

Expr *IndexExpr::clone(CloneCtx &map) {
  Loop *loop = map.get(this->loop_);
  return new IndexExpr(loop);
}

Stmt *Loop::clone(CloneCtx &map) {
  Loop *loop = new Loop(c_, end_, vf_);
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
  std::vector<Argument *> newArgs;
  for (auto *arg : args_) {
    Argument *newArg = new Argument(*arg);
    newArgs.push_back(newArg);
    map.map(arg, newArg);
  }
  return new Program((Scope *)body_->clone(map), newArgs);
}
