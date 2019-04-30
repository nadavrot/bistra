#include "bistra/Program/Program.h"
#include <iostream>

using namespace bistra;

int main() {
  Program p;
  p.addArgument("bar", {32, 32}, ElemKind::Float32Ty);
  p.addArgument("foo", {10, 32, 32, 4}, ElemKind::Float32Ty);
  auto *L = new Loop("i", 10, 1);
  auto *K = new Loop("j", 10, 1);
  L->addStmt(K);
  p.addStmt(L);

  auto *buff = &p.getArgs()[0];
  auto *ld = new LoadExpr(buff, {new IndexExpr(K), new IndexExpr(L)});
  auto *val = new AddExpr(ld, new ConstantExpr(4));
  auto *store =
      new StoreStmt(buff, {new IndexExpr(K), new IndexExpr(L)}, val, true);

  K->addStmt(store);

  p.dump();
  std::cout << std::endl;
}
