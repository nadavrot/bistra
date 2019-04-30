#include "bistra/Program/Program.h"
#include <iostream>

using namespace bistra;

int main() {
  Program p;
  p.addArgument("foo", {10, 32, 32, 4}, ElemKind::Float32Ty);
  p.addArgument("bar", {32, 32}, ElemKind::Float32Ty);
  auto *L = new Loop("i", 10, 1);
  auto *K = new Loop("j", 100, 1);
  L->addStmt(K);

  p.addStmt(L);
  p.dump();
  std::cout << std::endl;
}
