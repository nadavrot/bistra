#include "bistra/Program/Program.h"

#include <iostream>

using namespace bistra;

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
