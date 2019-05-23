#ifndef BISTRA_ANALYSIS_PROGRAM_H
#define BISTRA_ANALYSIS_PROGRAM_H

#include <cinttypes>

namespace bistra {

class Loop;

/// \returns the estimated the number of elements are loaded in a loop.
uint64_t getNumLoadsInLoop(Loop *L);

/// \returns the number of arithmetic operations in a loop.
uint64_t getNumArithmeticInLoop(Loop *L);

} // end namespace bistra

#endif
