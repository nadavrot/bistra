#ifndef BISTRA_PARSER_PARSERCONTEXT_H
#define BISTRA_PARSER_PARSERCONTEXT_H

#include <string>

namespace bistra {
class Program;

/// The context that serves the parser while parsing.
class ParserContext {
  /// The parsed program.
  Program *prog_{nullptr};

  /// Counts the number of errors that were emitted.
  unsigned numErrors_{0};

public:
  ParserContext() = default;

  /// Saves the parsed program when done.
  void registerProgram(Program *p);

  /// \returns the parsed program or nullptr.
  Program *getProgram() { return prog_; }

  /// \returns the number of errors.
  unsigned getNumErrors() { return numErrors_; }

  /// Emit an error message.
  /// TODO: add location information.
  void diagnose(const std::string &message);
};

} // namespace bistra

#endif // BISTRA_PARSER_PARSERCONTEXT_H
