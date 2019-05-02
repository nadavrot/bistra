#include "bistra/Backends/Backends.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/CBackend/CBackend.h"
using namespace bistra;

Backend *bistra::getBackend(const std::string &name) { return new CBackend(); }
