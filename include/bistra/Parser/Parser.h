#ifndef BISTRA_PARSER_PARSER_H
#define BISTRA_PARSER_PARSER_H

#include "bistra/Parser/ParserContext.h"
#include "bistra/Parser/Token.h"
#include "bistra/Program/Types.h"

namespace bistra {
class Lexer;
class Stmt;
class Scope;
class Expr;
struct Type;

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
  Parser(ParserContext &ctx);
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

  /// Read tokens until we get to one of the tokens \p A or \p B, then return
  /// without consuming it.
  void skipUntilOneOf(TokenKind A, TokenKind B);

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

  /// Parse the next token and update \p kind, or return true on error.
  bool parseBuiltinType(ElemKind &kind);

  /// Parse the name:value pair. Example: I:512
  bool parseTypePair(std::string &name, int &val);

  /// If the input is malformed, this emits the specified error diagnostic and
  /// returns true.
  bool parseNamedType(Type &T, std::string &name);

  /// Parse a single integer literal.
  bool parseIntegerLiteral(int &val);

  /// Parse a single integer literal or integer in a let constant.
  bool parseIntegerLiteralOrLetConstant(int &val);

  /// Parse a single float literal.
  bool parseFloatLiteral(double &val);

  /// Parse Identifier.
  bool parseIdentifier(std::string &text);

  /// Parse a list of indices acessing arrays.
  /// Example (the subscript part):  A[1, 2, j*2, i + 3 ]
  bool parseSubscriptList(std::vector<Expr *> &exprs);

  /// Parse and store the Let statement.
  bool parseLetStmt();

  /// Parse a single unit (stmt).
  Stmt *parseOneStmt();

  /// Parse the for stmt.
  Stmt *parseForStmt();

  /// Parse the for stmt.
  Stmt *parseIfStmt();

  /// Parse a Pragma attribute.
  Stmt *parsePragma();

  /// Parse literal or dimension, such as C.x;
  /// \returns True if an error occured.
  bool parseLiteralOrDimExpr(int &value);

  /// Parse a scope that starts and ends with braces and populate \p s.
  bool parseScope(Scope *s);

  /// The parser expects that 'K' is next in the input.  If so, it
  /// is consumed and false is returned.
  ///
  /// If the input is malformed, this emits the specified error diagnostic and
  /// returns true.
  bool parseToken(TokenKind K);
};

/// Parses the program in \p src and returns a valid program or nullptr.
Program *parseProgram(const char *src);

} // namespace bistra

#endif // BISTRA_PARSER_PARSER_H
