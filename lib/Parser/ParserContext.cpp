#include "bistra/Parser/ParserContext.h"

#include <iostream>

using namespace bistra;

void ParserContext::diagnose(const std::string &message) {
  std::cout << message << "\n";
  numErrors_++;
}

void ParserContext::registerProgram(Program *p) { prog_ = p; }
