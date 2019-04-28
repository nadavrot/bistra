#include "bistra/Program/Program.h"

#include <iostream>

using namespace bistra;

void Type::dump() {
  std::cout << "[";
  for (int i = 0, e = sizes_.size(); i < e; i++) {
    if (i != 0) {
      std::cout << ",";
    }
    std::cout << sizes_[i];
  }
  std::cout << "]:" << getElementName();
}
