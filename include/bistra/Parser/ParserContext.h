#ifndef BISTRA_PARSER_PARSERCONTEXT_H
#define BISTRA_PARSER_PARSERCONTEXT_H

#include <string>
#include <unordered_map>
#include <vector>

namespace bistra {
class Program;
class Argument;
class Loop;

/// The context that serves the parser while parsing.
class ParserContext {
  /// The base pointer for the parsed buffer.
  const char *buffer_;

  /// The parsed program.
  Program *prog_{nullptr};

  /// Counts the number of errors that were emitted.
  unsigned numErrors_{0};

  /// Indexes arguments by name.
  std::unordered_map<std::string, Argument *> argMap_;

  /// Contains the next of loops while parsing.
  std::vector<Loop *> loopNextStack_;

public:
  ParserContext(const char *buffer) : buffer_(buffer) {}

  const char *getBuffer() const { return buffer_; }

  /// \returns a loop by name \p name that is currently in the loop nest stack.
  Loop *getLoopByName(const std::string &name);

  /// Add the loop \p L to the loop stack.
  void pushLoop(Loop *L);

  /// Remove the top loop from the loop stack.
  Loop *popLoop();

  /// Registers a new argument.
  void registerNewArgument(Argument *arg);

  /// \returns the argument with the name \p name or nullptr.
  Argument *getArgumentByName(const std::string &name);

  /// Saves the parsed program when done.
  void registerProgram(Program *p);

  /// \returns the parsed program or nullptr.
  Program *getProgram() { return prog_; }

  /// \returns the number of errors.
  unsigned getNumErrors() { return numErrors_; }

  /// Emit an error message.
  void diagnose(const char *loc, const std::string &message);
};

} // namespace bistra

#endif // BISTRA_PARSER_PARSERCONTEXT_H
