#ifndef BISTRA_BACKENDS_BACKENDS_H
#define BISTRA_BACKENDS_BACKENDS_H

#include "bistra/Program/Program.h"

#include <memory>

namespace bistra {

class Backend;

/// \returns a compiler backend that is defined by \p name. The name of the
/// compiler backend must be valid.
std::unique_ptr<Backend> getBackend(const std::string &name);

} // namespace bistra

#endif // BISTRA_BACKENDS_BACKENDS_H
