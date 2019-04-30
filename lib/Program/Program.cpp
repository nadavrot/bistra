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
    std::cout << sizes_[i];
  }
  std::cout << ">";
}

void Argument::dump() {
  std::cout << name_ << ":";
  type_.dump();
}

Program::Program() : body_(new Scope()) {}

Program::~Program() { delete body_; }

void Program::addArgument(const std::string &name, std::vector<unsigned> dims,
                          ElemKind Ty) {
  Type t(Ty, dims);
  addArgument(Argument(name, t));
}

void Program::addArgument(const Argument &arg) { args_.push_back(arg); }

void Program::dump() {
  std::cout << "Program (";
  for (int i = 0, e = args_.size(); i < e; i++) {
    if (i != 0) {
      std::cout << ", ";
    }
    args_[i].dump();
  }
  std::cout << ") {\n";
  body_->dump(1);
  std::cout << "}";
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

void Index::dump() { std::cout << loop_->getName(); }
