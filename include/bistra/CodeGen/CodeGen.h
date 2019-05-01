#ifndef BISTRA_CODEGEN_CODEGEN_H
#define BISTRA_CODEGEN_CODEGEN_H

namespace bistra {

/// \returns a CPP program that represents the program \p P.
std::string emitCPP(Program &P);

} // namespace bistra

#endif // BISTRA_CODEGEN_CODEGEN_H
