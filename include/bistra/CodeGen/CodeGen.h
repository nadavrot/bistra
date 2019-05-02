#ifndef BISTRA_CODEGEN_CODEGEN_H
#define BISTRA_CODEGEN_CODEGEN_H

#include "bistra/Program/Program.h"

namespace bistra {

/// \returns a C++ program that represents \p P.
std::string emitCPP(Program &P);

} // namespace bistra

#endif // BISTRA_CODEGEN_CODEGEN_H
