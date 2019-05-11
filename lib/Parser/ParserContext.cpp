#include "bistra/Parser/ParserContext.h"
#include "bistra/Program/Program.h"

#include <iostream>

using namespace bistra;

Loop *ParserContext::getLoopByName(const std::string &name) {
  // Go over the loop nest in reverse to find the last decleration of the
  // variable with the index \p name.
  for (int i = loopNextStack_.size() - 1; i >= 0; i--) {
    Loop *L = loopNextStack_[i];
    if (L->getName() == name)
      return L;
  }
  return nullptr;
}

void ParserContext::pushLoop(Loop *L) { loopNextStack_.push_back(L); }

Loop *ParserContext::popLoop() {
  auto *L = loopNextStack_[loopNextStack_.size() - 1];
  loopNextStack_.pop_back();
  return L;
}

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
