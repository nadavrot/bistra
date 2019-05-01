#include "bistra/CodeGen/CodeGen.h"
#include "bistra/Program/Program.h"

#include <sstream>
#include <string>

using namespace bistra;

const char *header = R"(
typedef float float4 __attribute__((ext_vector_type(4)));
typedef float float2 __attribute__((ext_vector_type(2)));
)";

class cppEmitter {
  /// A string builder for
  std::stringstream sb_;

  void generate(Program &P);
};

std::string emitCPP(Program &P) {
  std::stringstream sb;
  return "";
}
