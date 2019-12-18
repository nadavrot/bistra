#ifndef BISTRA_PROGRAM_UTILS_H
#define BISTRA_PROGRAM_UTILS_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bistra {

class Scope;

/// Prints some useful statistics about the loops in the program and their
/// execution frequency.
void dumpProgramFrequencies(Scope *P);

/// Saves the content \p content to file \p filename or aborts.
void writeFile(const std::string &filename, const std::string &content);

/// \returns the content of file \p filename or aborts.
std::string readFile(const std::string &filename);

/// Print a large number into a small number with quantiti suffix, such as K, M,
/// G, etc.
std::string prettyPrintNumber(uint64_t num);

/// \return X right-rotate \bits times.
uint64_t ror(uint64_t x, unsigned int bits);

uint64_t hashJoin(uint64_t one, uint64_t two);

uint64_t hashJoin(uint64_t one, uint64_t two, uint64_t three);

uint64_t hashString(const std::string &str);

} // namespace bistra

#endif // BISTRA_PROGRAM_UTILS_H
