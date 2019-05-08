#include "bistra/Program/Program.h"

#include <iostream>

using namespace bistra;

void Type::dump() const {
  std::cout << getElementName() << "<";
  for (int i = 0, e = sizes_.size(); i < e; i++) {
    if (i != 0) {
      std::cout << ",";
    }
    std::cout << names_[i] << ":" << sizes_[i];
  }
  std::cout << ">";
}

void ExprType::dump() const {
  std::cout << "<" << getWidth() << " x " << getElementName() << ">";
}

std::string ExprType::getTypename() const {
  std::string res = std::string(getElementName());
  if (width_ > 1) {
    res += std::to_string(width_);
  }
  return res;
}
