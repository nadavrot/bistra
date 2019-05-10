#include "bistra/Parser/Parser.h"
#include "bistra/Parser/Lexer.h"
#include "bistra/Parser/ParserContext.h"

using namespace bistra;

Parser::Parser(const char *buffer, ParserContext &ctx)
    : L_(new Lexer(ctx, buffer)), ctx_(ctx) {}

Parser::~Parser() { delete L_; }

/// Return the precedence of the binary operator, or zero if the token is not
/// a binary operator.
static unsigned getBinOpPrecedence(TokenKind Kind) {
  switch (Kind) {
  default: { return 0; }
#define OPERATOR(ID, PREC)                                                     \
  case TokenKind::ID:                                                          \
    return PREC;
#include "bistra/Parser/BinaryOps.def"
#undef OPERATOR
  }
}

/// Returns true if the token is an operator.
static bool isOperator(TokenKind Kind) {
  switch (Kind) {
  default: { return false; }
#define OPERATOR(ID, PREC)                                                     \
  case TokenKind::ID:                                                          \
    return true;
#include "bistra/Parser/BinaryOps.def"
#undef OPERATOR
  }
}

void Parser::consumeToken() {
  assert(!Tok.is(eof) && "Lexing past eof!");
  L_->Lex(Tok);
}

void Parser::skipUntil(TokenKind T) {
  if (T == unknown) {
    return;
  }

  while (!Tok.is(eof) && !Tok.is(T)) {
    switch (Tok.getKind()) {
    default:
      consumeToken();
      break;
      // TODO: Handle paren/brace/bracket recovery.
    }
  }
}

bool Parser::parseTypePair(std::string &name, unsigned &val) {
  name = Tok.getText();

  if (!consumeIf(TokenKind::identifier)) {
    ctx_.diagnose("Expecting dimension name.");
    return true;
  }
  if (!consumeIf(TokenKind::colon)) {
    ctx_.diagnose("Expecting colon after dimension name " + name + ".");
    return true;
  }

  if (Tok.is(TokenKind::integer_literal)) {
    val = std::atoi(Tok.getText().c_str());
  }

  if (!consumeIf(TokenKind::integer_literal)) {
    ctx_.diagnose("Expecting integer after dimension name " + name + ".");
    return true;
  }

  return false;
}

// Example: C:float<I:512,J:512>,
bool Parser::parseNamedType(Type &T, std::string &name) {
  name = Tok.getText();

  if (!consumeIf(TokenKind::identifier)) {
    ctx_.diagnose("Expecting buffer argument name.");
    return true;
  }

  if (!consumeIf(TokenKind::colon)) {
    ctx_.diagnose("Expecting colon after typename: " + name + ".");
  }

  ElemKind scalarsTy;

  switch (Tok.getKind()) {
  case TokenKind::builtin_type_float:
    scalarsTy = ElemKind::Float32Ty;
    break;

  case TokenKind::builtin_type_int8:
    scalarsTy = ElemKind::Int8Ty;
    break;

  case TokenKind::builtin_type_index:
    scalarsTy = ElemKind::IndexTy;
    break;

  default:
    ctx_.diagnose(std::string("Expecting colon after typename") + name);
    return true;
  }
  consumeToken();

  if (!consumeIf(TokenKind::lt)) {
    ctx_.diagnose(std::string("Expecting dimension list"));
    return true;
  }

  std::vector<std::string> names;
  std::vector<unsigned> sizes;

  std::string dimName;
  unsigned dimVal;

  // Parse the first mandatory dimension.
  if (parseTypePair(dimName, dimVal)) {
    return true;
  }
  names.push_back(dimName);
  sizes.push_back(dimVal);

  // Parse the remaining optional dimensions.
  while (Tok.is(TokenKind::comma)) {
    consumeToken(TokenKind::comma);
    // Parse the first mandatory dimension.
    if (parseTypePair(dimName, dimVal)) {
      ctx_.diagnose(std::string("Expecting dimension definition"));
      skipUntil(TokenKind::gt);
      break;
    }
    names.push_back(dimName);
    sizes.push_back(dimVal);
  }

  if (!consumeIf(TokenKind::gt)) {
    ctx_.diagnose(std::string("Expecting dimension list"));
    skipUntil(TokenKind::gt);
  }

  T = Type(scalarsTy, sizes, names);
  return false;
}

Program *Parser::parseFunctionDecl() {
  if (!consumeIf(kw_def)) {
    skipUntil(TokenKind::eof);
    return nullptr;
  }

  std::string progName = Tok.getText();

  if (!consumeIf(TokenKind::identifier)) {
    ctx_.diagnose("expecting function name after def");
    return nullptr;
  }

  if (!Tok.is(l_paren)) {
    ctx_.diagnose("Expecting argument list after function name.");
    return nullptr;
  }

  consumeToken(l_paren);

  Program *p = new Program();

  Type T;
  std::string typeName;
  // Parse the mandatory first parameter.
  if (parseNamedType(T, typeName)) {
    return nullptr;
  }
  p->addArgument(new Argument(typeName, T));

  // Parse the optional argument list.
  while (Tok.is(TokenKind::comma)) {
    if (parseNamedType(T, typeName)) {
      return nullptr;
    }
    p->addArgument(new Argument(typeName, T));
  }

  if (!consumeIf(TokenKind::r_paren)) {
    ctx_.diagnose("Expecting the end of the argument list.");
    return nullptr;
  }

  if (!consumeIf(TokenKind::l_brace)) {
    ctx_.diagnose("Expecting function body.");
    return nullptr;
  }

  // TODO: parse function body.

  if (!consumeIf(TokenKind::r_brace)) {
    ctx_.diagnose("Expecting closing brace to function body.");
    return nullptr;
  }

  return p;
}

void Parser::Parse() {
  // Prime the Lexer!
  consumeToken();

  // Only allow function declerations in the top-level scope.
  if (Tok.is(kw_def)) {
    if (Program *func = parseFunctionDecl()) {
      ctx_.registerProgram(func);
    }

    if (!Tok.is(TokenKind::eof)) {
      ctx_.diagnose("Expecting eof of file after function.");
    }
    return;
  }

  ctx_.diagnose("Expecting function decleration.");
  return;
}
