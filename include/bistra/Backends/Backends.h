#ifndef BISTRA_BACKENDS_BACKENDS_H
#define BISTRA_BACKENDS_BACKENDS_H

#include "bistra/Program/Program.h"

namespace bistra {

class Backend;

Backend *getBackend(const std::string &name);

} // namespace bistra

#endif // BISTRA_BACKENDS_BACKENDS_H
