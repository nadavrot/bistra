#include "bistra/Backends/CBackend/CBackend.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"

#include <sstream>
#include <string>

using namespace bistra;

const char *header = R"(
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#if defined(__clang__)
using float4 = float __attribute__((ext_vector_type(4)));
using float8 = float __attribute__((ext_vector_type(8)));
#elif defined(__GNUC__) || defined(__GNUG__)
using float4 = float __attribute__((vector_size(16)));
using float8 = float __attribute__((vector_size(32)));
#endif
/// \returns the index of the element at x,y,z,w.
inline size_t btr_get4(const size_t *dims, size_t x, size_t y, size_t z,
                          size_t w) {
  return (x * dims[1] * dims[2] * dims[3]) + (y * dims[2] * dims[3]) +
  (z * dims[3]) + w;
}
/// \returns the index of the element at x,y,z.
inline size_t btr_get3(const size_t *dims, size_t x, size_t y, size_t z) {
  return (x * dims[1] * dims[2]) + (y * dims[2]) + z;
}
/// \returns the index of the element at x,y.
inline size_t btr_get2(const size_t *dims, size_t x, size_t y) {
  return (x * dims[1]) + y;
}

void s_capture(volatile char *ptr) { auto k = *ptr; }
)";

const char *benchmark = R"(
  double time_spent = 0.0;
  clock_t begin = clock();
  program(C, A, B);
  clock_t end = clock();
  time_spent += (double)(end - begin) / CLOCKS_PER_SEC;
  printf("Time elpased is %f seconds", time_spent);
)";

const char *benchmark_start = R"(
double time_spent = 0.0;
clock_t begin = clock();
)";

const char *benchmark_end = R"(
clock_t end = clock();
time_spent += (double)(end - begin) / CLOCKS_PER_SEC;
printf("Time elpased is %f seconds", time_spent);
)";

class cppEmitter {
  /// A string builder for
  std::stringstream sb_;

public:
  std::string getOutput() { return sb_.str(); }

  void generate(Expr *exp) {
    // Handle Index expressions.
    if (IndexExpr *ii = dynamic_cast<IndexExpr *>(exp)) {
      sb_ << "(" << ii->getLoop()->getName() << ")";
      return;
    }

    // Handle Constant expressions.
    if (ConstantExpr *cc = dynamic_cast<ConstantExpr *>(exp)) {
      sb_ << "(" << cc->getValue() << ")";
      return;
    }

    // Handle float-constant expressions.
    if (ConstantFPExpr *cc = dynamic_cast<ConstantFPExpr *>(exp)) {
      sb_ << "(" << cc->getValue() << ")";
      return;
    }

    // Handle binary expressions.
    if (BinaryExpr *bin = dynamic_cast<BinaryExpr *>(exp)) {
      std::string op;
      // Figure out which op this is.
      if (dynamic_cast<AddExpr *>(exp)) {
        op = ") + (";
      } else if (dynamic_cast<MulExpr *>(exp)) {
        op = ") * (";
      } else {
        assert(false && "unknown operator");
      }

      // Emit the binary expression.
      sb_ << "((";
      generate(bin->getLHS());
      sb_ << op;
      generate(bin->getRHS());
      sb_ << "))";
      return;
    }

    // Handle Load expressions.
    if (LoadExpr *ld = dynamic_cast<LoadExpr *>(exp)) {
      sb_ << "(";
      auto *buffer = ld->getDest();
      emitBudderIndex(buffer->getName(), ld->getIndices());
      sb_ << ")";
      return;
    }
    assert(false && "Unknown expression");
  }

  /// Generate the indexing expression into som buffer. Example:
  ///  C[btr_getXY(C_dims, i, j)]
  void emitBudderIndex(const std::string &varName,
                       const std::vector<Expr *> &indices) {
    sb_ << varName << "[btr_get" << indices.size() << "(" << varName << "_dims";
    for (auto *E : indices) {
      sb_ << ",";
      generate(E);
    }
    sb_ << ")]";
  }

  void generate(Stmt *stmt) {
    // Handle 'Scope'.
    if (Scope *scp = dynamic_cast<Scope *>(stmt)) {
      // Generate all of the statements in the scope.
      for (auto *s : scp->getBody()) {
        generate(s);
      }
      return;
    }

    // Handle 'Loop'.
    if (Loop *l = dynamic_cast<Loop *>(stmt)) {
      auto name = l->getName();
      sb_ << "for (size_t " << name << " = 0; " << name << " < " << l->getEnd()
          << "; " << name << "++) {\n";
      generate(l->getBody());
      sb_ << "}\n";
      return;
    }

    // Handle store statements.
    if (StoreStmt *st = dynamic_cast<StoreStmt *>(stmt)) {
      auto *buffer = st->getDest();
      emitBudderIndex(buffer->getName(), st->getIndices());
      sb_ << (st->isAccumulate() ? " +=" : "=");
      generate(st->getValue());
      sb_ << ";\n";
      return;
    }

    assert(false && "Unknown statement");
  }

  void generate(Program &P) {
    sb_ << header;

    // Print the function decleration and argument list.
    // Example:      void proogram(float *C, float* A, float* B) {
    sb_ << "void program(";
    bool first = true;
    for (auto *p : P.getArgs()) {
      if (!first) {
        sb_ << ",";
      }
      sb_ << p->getType()->getElementName() << " *" << p->getName();
      first = false;
    }
    sb_ << ") {\n";

    // Print the tensor shape decls:
    // Example:
    //  static size_t C_dims[] = { 128, 32};
    //  static size_t A_dims[] = { 128, 64};
    for (auto *p : P.getArgs()) {
      sb_ << "static size_t " << p->getName() << "_dims[] = {";
      auto shape = p->getType()->getDims();
      for (auto d : shape) {
        sb_ << d << ",";
      }
      sb_ << "};\n";
    }

    // Generate the body of the function.
    generate(P.getBody());
    sb_ << "}\n";

    // Emit the benchmark program:
    sb_ << "int bench() {\n";

    for (auto *p : P.getArgs()) {
      auto elemTy = p->getType()->getElementName();
      auto name = p->getName();
      auto size = p->getType()->getSize();
      sb_ << elemTy << " *" << name << " = (" << elemTy << "*) malloc(sizeof("
          << elemTy << ") * " << std::to_string(size) << ");\n";

      sb_ << "bzero(" << name << ", " << size << "* sizeof(" << elemTy
          << "));\n";
    }
    sb_ << benchmark_start;
    sb_ << "program(";

    first = true;
    for (auto *p : P.getArgs()) {
      auto name = p->getName();
      if (!first)
        sb_ << ",";
      sb_ << name;
      first = false;
    }
    sb_ << ");\n";
    for (auto *p : P.getArgs()) {
      auto name = p->getName();
      sb_ << "s_capture((char*)" << name << ");\n";
      first = false;
    }
    sb_ << benchmark_end;
    sb_ << "}\n";
  }
};

std::string CBackend::emitCode(Program &P) {
  cppEmitter ee;

  ee.generate(P);
  return ee.getOutput();
}
