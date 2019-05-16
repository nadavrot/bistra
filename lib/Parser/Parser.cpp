#include "bistra/Parser/Parser.h"
#include "bistra/Parser/Lexer.h"
#include "bistra/Parser/ParserContext.h"
#include "bistra/Program/Program.h"

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

bool Parser::parseTypePair(std::string &name, int &val) {
  if (parseIdentifier(name)) {
    ctx_.diagnose("Expecting dimension name.");
    return true;
  }

  if (!consumeIf(TokenKind::colon)) {
    ctx_.diagnose("Expecting colon after dimension name " + name + ".");
    return true;
  }

  if (parseIntegerLiteral(val)) {
    ctx_.diagnose("Expecting integer after dimension name " + name + ".");
    return true;
  }

  return false;
}

bool Parser::parseIntegerLiteral(int &val) {
  if (Tok.is(TokenKind::integer_literal)) {
    val = std::atoi(Tok.getText().c_str());
    consumeIf(TokenKind::integer_literal);
    return false;
  }
  return true;
}

bool Parser::parseFloatLiteral(double &val) {
  if (Tok.is(TokenKind::float_literal)) {
    val = std::atof(Tok.getText().c_str());
    consumeIf(TokenKind::float_literal);
    return false;
  }
  return true;
}

bool Parser::parseIdentifier(std::string &text) {
  if (Tok.is(TokenKind::identifier)) {
    text = Tok.getText();
    consumeIf(TokenKind::identifier);
    return false;
  }
  std::string varName;
  if (parseIdentifier(varName)) {
    ctx_.diagnose("Unexpected identifier.");
  }
  return true;
}

Expr *Parser::parseExpr(unsigned RBP) {
  // Parse a single expression.
  Expr *LHS = parseExprPrimary();

  while (1) {
    // Check the precedence of the next token. Notice that the next token does
    // not have to be a binary operator.
    unsigned LBP = getBinOpPrecedence(Tok.getKind());
    if (RBP >= LBP) {
      break;
    }

    // Save the binary operator.
    auto operatorSymbol = Tok.getText();

    // Remove the operand and continue parsing.
    consumeToken();

    Expr *RHS = parseExpr(LBP);
    if (!RHS) {
      return nullptr;
    }

#define GEN(str, sym, kind)                                                    \
  if (str == sym) {                                                            \
    LHS = new BinaryExpr(LHS, RHS, BinaryExpr::BinOpKind::kind);               \
    continue;                                                                  \
  }

    GEN(operatorSymbol, "+", Add);
    GEN(operatorSymbol, "*", Mul);
    GEN(operatorSymbol, "/", Div);
    GEN(operatorSymbol, "-", Sub);

#undef GEN

    ctx_.diagnose("Unsupported operator: '" + operatorSymbol + "'.");
    return nullptr;
  }

  return LHS;
}

Expr *Parser::parseExprPrimary() {
  switch (Tok.getKind()) {
  case integer_literal: {
    int num;
    parseIntegerLiteral(num);
    return new ConstantExpr(num);
  }

  case float_literal: {
    double num;
    parseFloatLiteral(num);
    return new ConstantFPExpr(num);
  }

  case identifier: {
    std::string varName;
    parseIdentifier(varName);

    // Check if this identifier is a loop index.
    if (Loop *L = ctx_.getLoopByName(varName)) {
      return new IndexExpr(L);
    }

    // Check if this is a buffer access.
    Argument *A = ctx_.getArgumentByName(varName);
    if (Tok.is(l_square)) {
      if (!A) {
        ctx_.diagnose("Unknown subscript argument " + varName + ".");
        return nullptr;
      }

      std::vector<Expr *> exprs;
      if (parseSubscriptList(exprs)) {
        return nullptr;
      }

      return new LoadExpr(A, exprs);
    }

    ctx_.diagnose("Unknown identifier: " + varName + ".");
    return nullptr;
  }

  case l_paren: {
    consumeToken(l_paren);

    if (Expr *subExpr = parseExpr()) {
      if (!Tok.is(r_paren)) {
        ctx_.diagnose("Expected right paren to close the expression.");
        return nullptr;
      }
      consumeToken(r_paren);
      return subExpr;
    }
    return nullptr;
  }

  default:
    ctx_.diagnose("Unknown expression.");
    return nullptr;
  }
}

bool Parser::parseSubscriptList(std::vector<Expr *> &exprs) {
  assert(exprs.empty() && "exprs list not empty");
  if (!consumeIf(TokenKind::l_square)) {
    ctx_.diagnose("Expecting left square brace for subscript.");
    return true;
  }

  while (true) {
    // Parse an expression in the expression list.
    if (Expr *e = parseExpr()) {
      exprs.push_back(e);
    } else {
      return true;
    }

    // Hit the closing subscript. Bail.
    if (Tok.is(TokenKind::r_square))
      break;

    if (Tok.is(TokenKind::comma)) {
      consumeToken(TokenKind::comma);
      continue;
    }

    ctx_.diagnose("Expecting comma or end of subscript.");
    return true;
  }

  consumeToken(TokenKind::r_square);
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
  int dimVal;

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

bool Parser::parseScope(Scope *scope) {
  if (!consumeIf(TokenKind::l_brace)) {
    ctx_.diagnose("Expecting left brace for scope body.");
    return true;
  }

  while (!Tok.is(TokenKind::r_brace)) {
    if (Stmt *S = parseOneStmt()) {
      scope->addStmt(S);
    } else {
      return true;
    }
  }

  if (!consumeIf(TokenKind::r_brace)) {
    ctx_.diagnose("Expecting closing brace to scope body.");
    return true;
  }
  return false;
}

Stmt *Parser::parseForStmt() {
  // "for"
  consumeToken(TokenKind::kw_for);

  // "("
  if (!consumeIf(TokenKind::l_paren)) {
    ctx_.diagnose("Expecting left paren in for loop.");
    return nullptr;
  }

  // Indentifier name.
  std::string indexName;
  if (parseIdentifier(indexName)) {
    ctx_.diagnose("Expecting index name in for loop.");
    return nullptr;
  }

  // "in" keyword.
  if (!consumeIf(TokenKind::kw_in)) {
    ctx_.diagnose("Expecting 'in' keyword in the for loop.");
    return nullptr;
  }

  int zero;
  if (parseIntegerLiteral(zero) || zero != 0) {
    ctx_.diagnose("Expecting '0' in the for base range.");
    return nullptr;
  }

  // ".." range keyword.
  if (!consumeIf(TokenKind::range)) {
    ctx_.diagnose("Expecting the '..' range in the for loop.");
    ctx_.diagnose("Remember the space between the zero and '..'");
    return nullptr;
  }

  int endRange = 0;
  if (parseLiteralOrDimExpr(endRange)) {
    ctx_.diagnose("Unable to parse loop range.");
    return nullptr;
  }

  // ")"
  if (!consumeIf(TokenKind::r_paren)) {
    ctx_.diagnose("Expecting right brace in for loop.");
    return nullptr;
  }

  // Create the loop.
  Loop *L = new Loop(indexName, endRange);

  ctx_.pushLoop(L);
  // Parse the body of the loop.
  if (parseScope(L)) {
    return nullptr;
  }
  auto *K = ctx_.popLoop();
  assert(K == L && "Popped an unexpected loop");
  return L;
}

Stmt *Parser::parseIfStmt() {
  // "if"
  consumeToken(TokenKind::kw_if);

  // "("
  if (!consumeIf(TokenKind::l_paren)) {
    ctx_.diagnose("Expecting left paren in for loop.");
    return nullptr;
  }

  Expr *indexVal = parseExpr();
  if (!indexVal) {
    return nullptr;
  }

  // "in" keyword.
  if (!consumeIf(TokenKind::kw_in)) {
    ctx_.diagnose("Expecting 'in' keyword in the for loop.");
    return nullptr;
  }

  int startRange = 0;
  if (parseLiteralOrDimExpr(startRange)) {
    ctx_.diagnose("Unable to parse if-range.");
    return nullptr;
  }

  // ".." range keyword.
  if (!consumeIf(TokenKind::range)) {
    ctx_.diagnose("Expecting the '..' range in the if-range loop.");
    ctx_.diagnose("Remember the space between the value and '..'");
    return nullptr;
  }

  int endRange = 0;
  if (parseLiteralOrDimExpr(endRange)) {
    ctx_.diagnose("Unable to parse if-range.");
    return nullptr;
  }

  // ")"
  if (!consumeIf(TokenKind::r_paren)) {
    ctx_.diagnose("Expecting right brace in for loop.");
    return nullptr;
  }

  // Create the if-range.
  IfRange *IR = new IfRange(indexVal, startRange, endRange);

  // Parse the body of the loop.
  if (parseScope(IR)) {
    return nullptr;
  }

  return IR;
}

bool Parser::parseLiteralOrDimExpr(int &value) {
  if (Tok.is(TokenKind::integer_literal)) {
    // End of index range.
    if (parseIntegerLiteral(value)) {
      ctx_.diagnose("Expecting an integer value.");
      return true;
    }

    return false;
  }

  if (Tok.is(identifier)) {
    std::string varName;
    if (parseIdentifier(varName)) {
      ctx_.diagnose("Expecting argument name.");
      return true;
    }

    Argument *arg = ctx_.getArgumentByName(varName);
    if (!arg) {
      ctx_.diagnose("Unexpected argument name in for loop range: " + varName);
      return true;
    }

    if (!consumeIf(TokenKind::period)) {
      ctx_.diagnose("Expecting a member access in loop range: " + varName);
      return true;
    }

    std::string dimName;
    if (parseIdentifier(dimName)) {
      ctx_.diagnose("Expecting dimension name in loop range: " + varName);
      return true;
    }

    value = arg->getType()->getDimSizeByName(dimName);
    if (value == 0) {
      ctx_.diagnose("Invalid dimension name in: " + varName + "." + dimName);
      return true;
    }

    return false;
  }

  ctx_.diagnose("Invalid expression in dimension name.");
  return true;
}

/// Parse a single unit (stmt).
Stmt *Parser::parseOneStmt() {

  /// Parse variable access:
  ///
  /// Example: A[1,I * 8, 12] += 2
  if (Tok.is(TokenKind::identifier)) {
    std::string varName;
    parseIdentifier(varName);

    Argument *arg = ctx_.getArgumentByName(varName);
    if (!arg) {
      ctx_.diagnose("Accessing unknown variable.");
      return nullptr;
    }

    if (Tok.is(l_square)) {
      if (!arg) {
        ctx_.diagnose("Unknown subscript argument " + varName + ".");
        return nullptr;
      }
      std::vector<Expr *> indices;
      if (parseSubscriptList(indices)) {
        return nullptr;
      }

      bool accumulate;

      switch (Tok.getKind()) {
      case TokenKind::plusEquals:
        consumeToken();
        accumulate = true;
        break;

      case TokenKind::assign:
        consumeToken();
        accumulate = false;
        break;

      default:
        ctx_.diagnose("Expecting assignment operator after buffer access.");
        return nullptr;
      }

      Expr *storedValue = parseExpr();
      if (!storedValue) {
        return nullptr;
      }

      return new StoreStmt(arg, indices, storedValue, accumulate);
    }

    ctx_.diagnose("Expecting subscript after identifier " + varName + ".");
    return nullptr;
  }

  // Parse for-statements:
  // Example: for (i in 0 .. 100) { ... }
  if (Tok.is(TokenKind::kw_for)) {
    return parseForStmt();
  }

  // Parse if-statements:
  // Example: if (i in 34 .. C.x) { ... }
  if (Tok.is(TokenKind::kw_if)) {
    return parseIfStmt();
  }

  ctx_.diagnose("Unknown statement in scope body.");
  return nullptr;
}

Program *Parser::parseFunctionDecl() {
  if (!consumeIf(kw_def)) {
    skipUntil(TokenKind::eof);
    return nullptr;
  }

  // Indentifier name.
  std::string progName = Tok.getText();
  if (parseIdentifier(progName)) {
    ctx_.diagnose("Expecting function name after def.");
    return nullptr;
  }

  if (!Tok.is(l_paren)) {
    ctx_.diagnose("Expecting argument list after function name.");
    return nullptr;
  }

  consumeToken(l_paren);

  Program *p = new Program(progName);

  Type T;
  std::string typeName;
  // Parse the mandatory first parameter.
  if (parseNamedType(T, typeName)) {
    return nullptr;
  }

  auto *firstArg = new Argument(typeName, T);
  p->addArgument(firstArg);
  ctx_.registerNewArgument(firstArg);

  // Parse the optional argument list.
  while (Tok.is(TokenKind::comma)) {
    consumeToken(TokenKind::comma);
    if (parseNamedType(T, typeName)) {
      return nullptr;
    }

    if (ctx_.getArgumentByName(typeName)) {
      ctx_.diagnose("Argument with this name already exists.");
      // Try to recover by ignoring this argument.
      continue;
    }

    auto *arg = new Argument(typeName, T);
    ctx_.registerNewArgument(arg);
    p->addArgument(arg);
  }

  if (!consumeIf(TokenKind::r_paren)) {
    ctx_.diagnose("Expecting the end of the argument list.");
    return nullptr;
  }

  if (parseScope(p)) {
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

Program *bistra::parseProgram(const char *src) {
  ParserContext ctx;
  Parser P(src, ctx);
  P.Parse();
  return ctx.getProgram();
}
