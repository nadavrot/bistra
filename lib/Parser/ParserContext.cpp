#include "bistra/Parser/ParserContext.h"
#include "bistra/Program/Program.h"

#include <iostream>

using namespace bistra;

void ParserContext::registerNewArgument(Argument *arg) {
  assert(getArgumentByName(arg->getName()) == nullptr &&
         "Argument already registered");
  argMap_[arg->getName()] = arg;
}

Argument *ParserContext::getArgumentByName(const std::string &name) {
  auto ir = argMap_.find(name);
  if (ir == argMap_.end()) {
    return nullptr;
  }

  return ir->second;
}

void ParserContext::diagnose(const std::string &message) {
  std::cout << message << "\n";
  numErrors_++;
}

void ParserContext::registerProgram(Program *p) { prog_ = p; }
