#ifndef BISTRA_PARSER_PARSERCONTEXT_H
#define BISTRA_PARSER_PARSERCONTEXT_H

#include "bistra/Program/Pragma.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace bistra {
class Program;
class Expr;
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

  /// A list of pragma declerations.
  std::vector<PragmaCommand> pragmas_;

  /// Indexes arguments by name.
  std::unordered_map<std::string, Argument *> argMap_;

  /// Contains the next of loops while parsing.
  std::vector<Loop *> loopNextStack_;

  /// Contains the next of loops while parsing.
  std::vector<std::pair<std::string, Expr *>> letStack_;

public:
  ParserContext(const char *buffer) : buffer_(buffer) {}

  const char *getBuffer() const { return buffer_; }

  /// \returns a loop by name \p name that is currently in the loop nest stack.
  Loop *getLoopByName(const std::string &name);

  /// Add the loop \p L to the loop stack.
  void pushLoop(Loop *L);

  /// Remove the top loop from the loop stack.
  Loop *popLoop();

  /// \returns the size of the stack that keeps the 'let' expression. This
  /// allows us to unwind and get rid of unused lets when the 'let' go out of
  /// scope.
  unsigned getLetStackLevel() const;

  /// Restore the 'let' stack to the level of \p handle.
  void restoreLetStack(unsigned handle);

  /// \returns the stored expression or nullptr if the name was not found.
  Expr *getLetExprByName(const std::string &name) const;

  /// Register the expression \p e under the name \p name.
  void registerLetValue(const std::string &name, Expr *e);

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

  /// \returns the pragma declerations that were applied to loops.
  std::vector<PragmaCommand> &getPragmaDecls() { return pragmas_; }

  /// Adds the pragma to the list of applied pragmas.
  void addPragma(PragmaCommand &pc);

  /// Emit an error message.
  void diagnose(const char *loc, const std::string &message);
};

} // namespace bistra

#endif // BISTRA_PARSER_PARSERCONTEXT_H
