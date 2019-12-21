#ifndef BISTRA_PARSER_LEXER_H
#define BISTRA_PARSER_LEXER_H

#include "bistra/Parser/ParserContext.h"
#include "bistra/Parser/Token.h"

#include <cassert>

namespace bistra {
class Token;
class ParserContext;

/// Lexer - converts the char sequence to tokens.
class Lexer {
  /// The base pointer to the buffer that we lex.
  const char *buffer_;
  /// Pointer to the current position.
  const char *curPtr_;
  /// Parser context used to emit errors.
  ParserContext &context_;

  Lexer(const Lexer &) = delete;
  void operator=(const Lexer &) = delete;

public:
  /// Initialize the Lexer.
  Lexer(ParserContext &ctx);
  /// Lex the next token.
  void Lex(Token &result);

  /// \returns the lexed buffer.
  const char *getBuffer() const { return buffer_; }

private:
  // Skip comments.
  void skipSlashSlashComment();
  // Create a new token  in \p result.
  void formToken(TokenKind kind, const char *tokStart, Token &result);
  // Lex an identifier.
  void lexIdentifier(Token &result);
  // Lex a number.
  void lexNumber(Token &result);
  // Lex a string.
  void lexString(Token &result);
};

} // end namespace bistra

#endif
