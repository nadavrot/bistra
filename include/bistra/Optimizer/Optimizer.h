#ifndef BISTRA_OPTIMIZER_OPTIMIZER_H
#define BISTRA_OPTIMIZER_OPTIMIZER_H

#include "bistra/Program/Program.h"

#include <string>

namespace bistra {

class Program;
class Backend;

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
  std::unique_ptr<Backend> backend_;
  /// Save the best C program to this optional path, if not empty.
  std::string savePath_;

public:
  EvaluatorPass(std::unique_ptr<Backend> backend,
                const std::string &savePath = "")
      : Pass("evaluator", nullptr), bestProgram_(nullptr, nullptr),
        backend_(std::move(backend)), savePath_(savePath) {}
  virtual void doIt(Program *p) override;
  Program *getBestProgram() { return (Program *)bestProgram_.get(); }
};

class FilterPass : public Pass {
public:
  FilterPass(Pass *next) : Pass("filter", next) {}
  virtual void doIt(Program *p) override;
};

class VectorizerPass : public Pass {
public:
  VectorizerPass(Pass *next) : Pass("vectorizer", next) {}
  virtual void doIt(Program *p) override;
};

class InterchangerPass : public Pass {
public:
  InterchangerPass(Pass *next) : Pass("interchange", next) {}
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

class DistributePass : public Pass {
public:
  DistributePass(Pass *next) : Pass("distribute", next) {}
  virtual void doIt(Program *p) override;
};

/// Construct an optimization pipeline and evaluate different configurations for
/// the program \p. Save intermediate results to \p filename.
/// \returns the best program.
Program *optimizeEvaluate(std::unique_ptr<Backend> backend, Program *p,
                          const std::string &filename);

} // namespace bistra

#endif // BISTRA_OPTIMIZER_OPTIMIZER_H
