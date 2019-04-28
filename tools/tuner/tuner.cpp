#include "bistra/Program/Program.h"
#include <iostream>

using namespace bistra;

int main() {
  Type x(ElemKind::Float32Ty, {1, 2, 3, 4, 5, 6});

  x.dump();
  std::cout << std::endl;

  Program p;
  p.addArgument("foo", {10, 32, 32, 4}, ElemKind::Float32Ty);
  p.addArgument("bar", {32, 32}, ElemKind::Float32Ty);
  p.dump();
  std::cout << std::endl;

  Loop L("i", 10, 1, 1);
  L.dump();
}
