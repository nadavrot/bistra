#include "bistra/Backends/Backends.h"
#include "bistra/Backends/Backend.h"
#include "bistra/Backends/CBackend/CBackend.h"
#include "bistra/Backends/LLVMBackend/LLVMBackend.h"
using namespace bistra;

std::unique_ptr<Backend> bistra::getBackend(const std::string &name) {
  return std::make_unique<LLVMBackend>();
}
