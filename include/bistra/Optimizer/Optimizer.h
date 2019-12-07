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
  Backend &backend_;
  /// Save the best C program to this optional path, if not empty.
  std::string savePath_;
  /// Is the format textual?
  bool isText_;
  // Is the saved format bytecode?
  bool isBytecode_;

public:
  EvaluatorPass(Backend &backend, const std::string &savePath, bool isText,
                bool isBytecode)
      : Pass("evaluator", nullptr), bestProgram_(nullptr, nullptr),
        backend_(backend), savePath_(savePath), isText_(isText),
        isBytecode_(isBytecode) {}
  virtual void doIt(Program *p) override;
  Program *getBestProgram() { return (Program *)bestProgram_.get(); }
};

class FilterPass : public Pass {
  Backend &backend_;

public:
  FilterPass(Backend &backend, Pass *next)
      : Pass("filter", next), backend_(backend) {}
  virtual void doIt(Program *p) override;
};

class VectorizerPass : public Pass {
  Backend &backend_;

public:
  VectorizerPass(Backend &backend, Pass *next)
      : Pass("vectorizer", next), backend_(backend) {}
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
  Backend &backend_;

public:
  WidnerPass(Backend &backend, Pass *next)
      : Pass("widner", next), backend_(backend) {}
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
Program *optimizeEvaluate(Backend &backend, Program *p,
                          const std::string &filename, bool isTextual,
                          bool isBytecode);

} // namespace bistra

#endif // BISTRA_OPTIMIZER_OPTIMIZER_H
