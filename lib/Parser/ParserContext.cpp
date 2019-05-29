#include "bistra/Parser/ParserContext.h"
#include "bistra/Base/Base.h"
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

void ParserContext::addPragma(PragmaCommand &pc) { pragmas_.push_back(pc); }

std::pair<unsigned, unsigned> ParserContext::getLineCol(DebugLoc pos) {
  const char *p = buffer_;
  const char *end = pos.getStart();
  const char *bufferEnd = buffer_ + strlen(buffer_);
  assert(bufferEnd > end && "Invalid buffer position");
  unsigned line = 0;
  unsigned col = 0;
  while (p < end) {
    col++;
    p++;
    if (*p == '\n') {
      col = 0;
      line++;
    }
  }
  return {line, col};
}

void ParserContext::diagnose(DiagnoseKind kind, DebugLoc loc,
                             const std::string &message) {

  auto cur = getLineCol(loc);
  std::cout << filename_ << ":" << cur.first << ":" << cur.second << ":";
  // Print the error message.
  switch (kind) {
  case DiagnoseKind::Error:
    std::cout << " error: " << message << "\n";
    numErrors_++;
    break;
  case Warning:
    std::cout << " warning: " << message << "\n";
    numWarnings_++;
    break;
  case Note:
    std::cout << " note: " << message << "\n";
    numNotes_++;
    break;
  }

  const char *start = loc.getStart();
  const char *end = loc.getStart();
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
  while (start != loc.getStart()) {
    start++;
    std::cout << " ";
  }
  std::cout << "^\n\n";
}

void ParserContext::registerProgram(Program *p) { prog_ = p; }
