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

unsigned ParserContext::getLetStackLevel() const { return letStack_.size(); }

void ParserContext::restoreLetStack(unsigned handle) {
  assert(letStack_.size() >= handle && "Invalid state");
  while (letStack_.size() > handle) {
    delete letStack_.back().second;
    letStack_.pop_back();
  }
}

Expr *ParserContext::getLetExprByName(const std::string &name) const {
  // Go over the 'let' expression in reverse to find the last decleration of the
  // variable.
  for (int i = letStack_.size() - 1; i >= 0; i--) {
    auto let = letStack_[i];
    if (let.first == name)
      return let.second;
  }
  return nullptr;
}

void ParserContext::registerLetValue(const std::string &name, Expr *e) {
  letStack_.push_back(std::make_pair(name, e));
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

void ParserContext::addPragma(PragmaCommand &pc) { pragmas_.push_back(pc); }

void ParserContext::diagnose(const char *loc, const std::string &message) {
  const char *start = loc;
  const char *end = loc;
  // Find the start of the line.
  while (start != buffer_ && *(start - 1) != '\n') {
    start--;
  }

  // Find the end of the line.
  while (*end != 0 && *end != '\n') {
    end++;
  }

  // Print the context line.
  std::string line(start, end);
  std::cout << line << "\n";

  // Print the error marker.
  while (start != loc) {
    start++;
    std::cout << " ";
  }
  std::cout << "^\n";

  // Print the error message.
  std::cout << "Error:" << message << "\n";
  numErrors_++;
}

void ParserContext::registerProgram(Program *p) { prog_ = p; }
