#ifndef BISTRA_OPTIMIZER_OPTIMIZER_H
#define BISTRA_OPTIMIZER_OPTIMIZER_H

#include "bistra/Program/Program.h"

#include <string>

namespace bistra {

class Program;

class Pass {
  std::string name_;

protected:
  Pass *nextPass_;

public:
  Pass(const std::string &name, Pass *next) : name_(name), nextPass_(next) {}
  virtual void doIt(Program *p) = 0;
};

class EvaluatorPass : public Pass {
  double bestTime_{1000};
  StmtHandle bestProgram_;

public:
  EvaluatorPass()
      : Pass("evaluator", nullptr), bestProgram_(nullptr, nullptr) {}
  virtual void doIt(Program *p) override;
  Program *getBestProgram() { return (Program *)bestProgram_.get(); }
};

class VectorizerPass : public Pass {
public:
  VectorizerPass(Pass *next) : Pass("vectorizer", next) {}
  virtual void doIt(Program *p) override;
};

class TilerPass : public Pass {
public:
  TilerPass(Pass *next) : Pass("tiler", next) {}
  virtual void doIt(Program *p) override;
};

class WidnerPass : public Pass {
public:
  WidnerPass(Pass *next) : Pass("widner", next) {}
  virtual void doIt(Program *p) override;
};

class PromoterPass : public Pass {
public:
  PromoterPass(Pass *next) : Pass("promoter", next) {}
  virtual void doIt(Program *p) override;
};

/// Construct an optimization pipeline and evaluate different configurations for
/// the program \p. \returns the best program.
Program *optimizeEvaluate(Program *p);

} // namespace bistra

#endif // BISTRA_OPTIMIZER_OPTIMIZER_H
