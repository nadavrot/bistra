#include "bistra/Program/Program.h"

#include <iostream>

using namespace bistra;

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
  std::cout << ") {";
  std::cout << "}";
}

void Loop::dump() {
  std::cout << "for (" << c_ << " in 0.." << end_ << ", VF=" << vf_
            << ", UF=" << uf_ << ") {\n";
  std::cout << "}\n";
}
