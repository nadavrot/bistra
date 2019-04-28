#include "bistra/Program/Program.h"
#include <iostream>

using namespace bistra;

int main() {
  Type x(ElemKind::Float32Ty, {1, 2, 3, 4, 5, 6});

  x.dump();
  std::cout << std::endl;
}
