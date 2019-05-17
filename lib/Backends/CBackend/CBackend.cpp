#include "bistra/Backends/CBackend/CBackend.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Program/Program.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
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
typedef float __attribute__((ext_vector_type(4))) float4;
typedef float __attribute__((ext_vector_type(8))) float8;
typedef float __attribute__((ext_vector_type(16))) float16;
#elif defined(__GNUC__) || defined(__GNUG__)
typedef float __attribute__((vector_size(16))) float4;
typedef float __attribute__((vector_size(32))) float8;
typedef float __attribute__((vector_size(64))) float16;
#endif

#define defineVectorFunctions(SCALARTY, VECTORTY) \
inline VECTORTY Load_##VECTORTY(const SCALARTY *p) { \
        VECTORTY res; memcpy(&res, p, sizeof(VECTORTY)); return res; } \
inline void Store_##VECTORTY(SCALARTY *p,VECTORTY v) {\
        memcpy(p, &v, sizeof(VECTORTY)); } \
inline void Add_##VECTORTY(SCALARTY *p, VECTORTY v) {\
        Store_##VECTORTY(p, Load_##VECTORTY(p) + v);} \
inline VECTORTY Broadcast_##VECTORTY(SCALARTY s) { return (VECTORTY) s; }

defineVectorFunctions(float, float4)
defineVectorFunctions(float, float8)
defineVectorFunctions(float, float16)

inline bool IsInRange(size_t idx, size_t start, size_t end) {
  return (idx >= start && idx < end);
}
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
/// \returns the index of the element at x.
inline size_t btr_get1(const size_t *dims, size_t x) { return x; }

void s_capture(volatile char *ptr) { char k = *ptr; }
)";

const char *benchmark_start = R"(
double time_spent = 0.0;
clock_t begin = clock();
)";

const char *benchmark_end = R"(
clock_t end = clock();
time_spent += (double)(end - begin) / CLOCKS_PER_SEC;
printf("%f seconds elpased running %d iterations.\n",
       time_spent/benchmark_iterations,
       benchmark_iterations);
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
      // Emit the binary expression.
      sb_ << "((";
      generate(bin->getLHS());
      sb_ << ") " << bin->getOpSymbol() << " (";
      generate(bin->getRHS());
      sb_ << "))";
      return;
    }

    // Handle Load expressions.
    if (LoadExpr *ld = dynamic_cast<LoadExpr *>(exp)) {
      if (ld->getType().isVector()) {
        sb_ << "Load_" << ld->getType().getTypename() << "(&";
        auto *buffer = ld->getDest();
        emitBudderIndex(buffer->getName(), ld->getIndices());
        sb_ << ") ";
        return;
      }

      sb_ << "(";
      auto *buffer = ld->getDest();
      emitBudderIndex(buffer->getName(), ld->getIndices());
      sb_ << ")";
      return;
    }

    // Handle broadcast expressions.
    if (BroadcastExpr *bb = dynamic_cast<BroadcastExpr *>(exp)) {
      sb_ << "Broadcast_" << bb->getType().getTypename() << "(";
      generate(bb->getValue());
      sb_ << ")";
      return;
    }

    // Handle restore expressions.
    if (LoadLocalExpr *r = dynamic_cast<LoadLocalExpr *>(exp)) {
      sb_ << "(" << r->getDest()->getName() << ")";
      return;
    }

    assert(false && "Unknown expression");
  }

  /// Generate the indexing expression into som buffer. Example:
  ///  C[btr_getXY(C_dims, i, j)]
  void emitBudderIndex(const std::string &varName,
                       const std::vector<ExprHandle> &indices) {
    sb_ << varName << "[btr_get" << indices.size() << "(" << varName << "_dims";
    for (auto &E : indices) {
      sb_ << ",";
      generate(E.get());
    }
    sb_ << ")]";
  }

  void generate(Stmt *stmt) {
    // Handle 'Loop'.
    if (Loop *l = dynamic_cast<Loop *>(stmt)) {
      auto name = l->getName();
      unsigned increment = l->getStride();
      sb_ << "for (size_t " << name << " = 0; " << name << " < " << l->getEnd()
          << "; " << name << "+=" << increment << ") {\n";
      // Generate all of the statements in the scope.
      for (auto &SH : l->getBody()) {
        generate(SH.get());
      }
      sb_ << "}\n";
      return;
    }

    if (IfRange *ir = dynamic_cast<IfRange *>(stmt)) {
      auto range = ir->getRange();

      sb_ << "if (IsInRange(";
      generate(ir->getIndex());
      sb_ << ", " << range.first << ", " << range.second << ")) {\n";
      // Generate all of the statements in the scope.
      for (auto &SH : ir->getBody()) {
        generate(SH.get());
      }
      sb_ << "}\n";
      return;
    }

    // Handle store statements.
    if (StoreStmt *st = dynamic_cast<StoreStmt *>(stmt)) {
      auto Ty = st->getValue()->getType();

      if (st->getValue()->getType().isVector()) {
        sb_ << (st->isAccumulate() ? " Add_" : "Store_");
        sb_ << Ty.getTypename();
        sb_ << "(&";
        auto *buffer = st->getDest();
        emitBudderIndex(buffer->getName(), st->getIndices());
        sb_ << ", ";
        generate(st->getValue());
        sb_ << ");\n";
        return;
      }

      auto *buffer = st->getDest();
      emitBudderIndex(buffer->getName(), st->getIndices());
      sb_ << (st->isAccumulate() ? " +=" : "=");
      generate(st->getValue());
      sb_ << ";\n";
      return;
    }

    // Handle save to local statements.
    if (StoreLocalStmt *st = dynamic_cast<StoreLocalStmt *>(stmt)) {
      sb_ << st->getDest()->getName();
      sb_ << (st->isAccumulate() ? " +=" : "=");
      generate(st->getValue());
      sb_ << ";\n";
      return;
    }

    assert(false && "Unknown statement");
  }

  void generate(Program *P) {
    sb_ << header;

    // Print the function decleration and argument list.
    // Example:      void proogram(float *C, float* A, float* B) {
    sb_ << "void program(";
    bool first = true;
    for (auto *p : P->getArgs()) {
      if (!first) {
        sb_ << ",";
      }
      sb_ << p->getType()->getElementName() << "* __restrict__ "
          << p->getName();
      first = false;
    }
    sb_ << ") {\n";

    for (auto *loc : P->getVars()) {
      sb_ << loc->getType().getTypename() << " " << loc->getName() << ";\n";
    }

    // Print the tensor shape decls:
    // Example:
    //  static size_t C_dims[] = { 128, 32};
    //  static size_t A_dims[] = { 128, 64};
    for (auto *p : P->getArgs()) {
      sb_ << "static size_t " << p->getName() << "_dims[] = {";
      auto shape = p->getType()->getDims();
      for (auto d : shape) {
        sb_ << d << ",";
      }
      sb_ << "};\n";
    }

    // Generate all of the statements in the program.
    for (auto &SH : P->getBody()) {
      generate(SH.get());
    }
    sb_ << "}\n";
  }

  // Emit the benchmark for program \p P. Run \p iter iterations.
  void generateBenchmark(Program *P, unsigned iter) {
    sb_ << "int main() {\n";
    sb_ << "unsigned benchmark_iterations = " << iter << ";\n";
    for (auto *p : P->getArgs()) {
      auto elemTy = p->getType()->getElementName();
      auto name = p->getName();
      auto size = p->getType()->getSize();
      sb_ << elemTy << " *" << name << " = (" << elemTy << "*) malloc(sizeof("
          << elemTy << ") * " << std::to_string(size) << ");\n";

      sb_ << "bzero(" << name << ", " << size << "* sizeof(" << elemTy
          << "));\n";
    }
    sb_ << benchmark_start;
    sb_ << "for(int i = 0; i < benchmark_iterations; i++)";
    sb_ << "  program(";

    bool first = true;
    for (auto *p : P->getArgs()) {
      auto name = p->getName();
      if (!first)
        sb_ << ",";
      sb_ << name;
      first = false;
    }
    sb_ << ");\n";
    for (auto *p : P->getArgs()) {
      auto name = p->getName();
      sb_ << "s_capture((char*)" << name << ");\n";
    }
    sb_ << benchmark_end;
    sb_ << "}\n";
  }
};

std::string CBackend::emitProgramCode(Program *P) {
  cppEmitter ee;
  ee.generate(P);
  return ee.getOutput();
}

std::string CBackend::emitBenchmarkCode(Program *p, unsigned iter) {
  cppEmitter ee;
  ee.generate(p);
  ee.generateBenchmark(p, iter);
  return ee.getOutput();
}

static std::string shellExec(const std::string &cmd) {
  std::array<char, 1024> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"),
                                                pclose);
  if (!pipe) {
    std::cout << "popen() failed!\n";
    abort();
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

double CBackend::evaluateCode(Program *p, unsigned iter) {
  std::string tmpSrcName = std::string(std::tmpnam(nullptr)) + ".cpp";
  std::string tmpBinName = std::string(std::tmpnam(nullptr)) + ".bin";
  auto content = emitBenchmarkCode(p, iter);
  std::ofstream out(tmpSrcName);
  out << content;
  out.close();

  // Compile:
  shellExec(std::string("clang -mavx2 -Ofast ") + tmpSrcName + " -o " +
            tmpBinName);

  std::string::size_type sz;
  double timeInSeconds = 10000;
  // Execute:
  try {
    std::string timeReport = shellExec(tmpBinName);
    timeInSeconds = std::stod(timeReport, &sz);
  } catch (...) {
  }
  // Return the time in sec that the program measured internally and reported.
  return timeInSeconds;
}
