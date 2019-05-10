#ifndef BISTRA_PARSER_PARSER_H
#define BISTRA_PARSER_PARSER_H

#include "bistra/Parser/ParserContext.h"
#include "bistra/Parser/Token.h"
#include "bistra/Program/Program.h"

namespace bistra {
class Lexer;
class Stmt;
class Scope;

/// The program parser.
class Parser {
  // Lexer.
  Lexer *L_;
  /// Parser context.
  ParserContext &ctx_;
  /// The current token being considered.
  Token Tok;

  Parser(const Parser &) = delete;
  void operator=(const Parser &) = delete;

public:
  Parser(const char *buffer, ParserContext &ctx);
  ~Parser();

  /// The entry point for starting the parsing phase.
  void Parse();

  /// Expose the ASTContext for crash log utils.
  ParserContext &getContext() { return ctx_; }

private:
  /// Consume the current token.
  void consumeToken();

  // Consume the current token and verify that it is of kind \p k.
  void consumeToken(TokenKind K) {
    assert(Tok.is(K) && "Consuming wrong token kind");
    consumeToken();
  }

  /// If the current token is the specified kind, consume it and
  /// return true. Otherwise, return false without consuming it.
  bool consumeIf(TokenKind K) {
    if (!Tok.is(K)) {
      return false;
    }
    consumeToken(K);
    return true;
  }

  /// Read tokens until we get to the specified token, then return
  /// without consuming it.
  void skipUntil(TokenKind T);

  Expr *parseExprBinaryRHS(Expr *InputLHS, unsigned MinPrec = 1);

  /// Parse a simple expression.
  Expr *parseExprPrimary();

  /// Parses expressions.
  /// Parse the right hand side of a binary expression and assemble it according
  /// to precedence rules. \p RBP is the incoming operator precedence.
  ///
  /// Pratt's algorithm:
  /// http://eli.thegreenplace.net/2010/01/02/top-down-operator-precedence-parsing
  ///
  /// And also Clang's "EvaluateDirectiveSubExpr".
  Expr *parseExpr(unsigned RBP = 1);

  Program *parseFunctionDecl();

  /// Parse the name:value pair. Example: I:512
  bool parseTypePair(std::string &name, unsigned &val);

  /// If the input is malformed, this emits the specified error diagnostic and
  /// returns true.
  bool parseNamedType(Type &T, std::string &name);

  /// Parse a single integer literal.
  bool parseIntegerLiteral(int &val);

  /// Parse a single unit (stmt).
  Stmt *parseOneStmt();

  /// Parse a scope that starts and ends with braces and populate \p s.
  bool parseScope(Scope *s);

  /// The parser expects that 'K' is next in the input.  If so, it
  /// is consumed and false is returned.
  ///
  /// If the input is malformed, this emits the specified error diagnostic and
  /// returns true.
  bool parseToken(TokenKind K);
};

} // namespace bistra

#endif // BISTRA_PARSER_PARSER_H
