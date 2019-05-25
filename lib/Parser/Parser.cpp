#include "bistra/Parser/Parser.h"
#include "bistra/Parser/Lexer.h"
#include "bistra/Parser/ParserContext.h"
#include "bistra/Program/Pragma.h"
#include "bistra/Program/Program.h"

using namespace bistra;

using DiagnoseKind = ParserContext::DiagnoseKind;

Parser::Parser(ParserContext &ctx) : L_(new Lexer(ctx)), ctx_(ctx) {}

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
    }
  }
}

void Parser::skipUntilOneOf(TokenKind A, TokenKind B) {
  while (!Tok.is(eof) && !Tok.is(A) && !Tok.is(B)) {
    switch (Tok.getKind()) {
    default:
      consumeToken();
      break;
    }
  }
}

bool Parser::parseTypePair(std::string &name, int &val) {
  if (parseIdentifier(name)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting dimension name.");
    return true;
  }

  if (!consumeIf(TokenKind::colon)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting colon after dimension name " + name + ".");
    return true;
  }

  if (parseIntegerLiteralOrLetConstant(val)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting integer or constant after dimension name " + name +
                      ".");
    return true;
  }

  return false;
}

bool Parser::parseIntegerLiteral(int &val) {
  if (Tok.is(TokenKind::integer_literal)) {
    val = std::atoi(Tok.getText().c_str());
    consumeToken(TokenKind::integer_literal);
    return false;
  }
  return true;
}

bool Parser::parseIntegerLiteralOrLetConstant(int &val) {
  // Parse integers.
  if (!parseIntegerLiteral(val))
    return false;

  // Parse let literals.
  if (Tok.is(identifier)) {
    auto loc = Tok.getLoc();
    std::string varName;
    parseIdentifier(varName);

    // Is this a 'let' variable that contains an integer?
    if (auto *E = ctx_.getLetExprByName(varName)) {
      if (ConstantExpr *C = dynamic_cast<ConstantExpr *>(E)) {
        val = C->getValue();
        return false;
      }
      ctx_.diagnose(DiagnoseKind::Error, loc,
                    "variable '" + varName + "' is not a simple constant");
      return true;
    }
    ctx_.diagnose(DiagnoseKind::Error, loc,
                  "Unknown identifier '" + varName + "'");
    return true;
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
    consumeToken(TokenKind::identifier);
    return false;
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
    auto loc = Tok.getLoc();
    consumeToken();

    Expr *RHS = parseExpr(LBP);
    if (!RHS) {
      return nullptr;
    }

#define GEN(str, sym, kind)                                                    \
  if (str == sym) {                                                            \
    if (!LHS->getType().isEqual(RHS->getType())) {                             \
      ctx_.diagnose(DiagnoseKind::Error, loc, "operator types mismatch");      \
      return nullptr;                                                          \
    }                                                                          \
    LHS = new BinaryExpr(LHS, RHS, BinaryExpr::BinOpKind::kind, loc);          \
    continue;                                                                  \
  }

    GEN(operatorSymbol, "+", Add);
    GEN(operatorSymbol, "*", Mul);
    GEN(operatorSymbol, "/", Div);
    GEN(operatorSymbol, "-", Sub);

#undef GEN

    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "unsupported operator: '" + operatorSymbol + "'.");
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
    auto argLoc = Tok.getLoc();
    std::string varName;
    parseIdentifier(varName);

    // Check if this identifier is a loop index.
    if (Loop *L = ctx_.getLoopByName(varName)) {
      return new IndexExpr(L, argLoc);
    }

    // Check if this is a buffer access.
    Argument *A = ctx_.getArgumentByName(varName);
    if (Tok.is(l_square)) {
      if (!A) {
        ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                      "unknown subscript argument " + varName + ".");
        return nullptr;
      }

      std::vector<Expr *> exprs;
      if (parseSubscriptList(exprs)) {
        return nullptr;
      }

      // Check that the number of subscript arguments is correct.
      if (A->getType()->getNumDims() != exprs.size()) {
        ctx_.diagnose(DiagnoseKind::Error, argLoc,
                      "Invalid number of indices for buffer subscript.");
        return nullptr;
      }

      return new LoadExpr(A, exprs, argLoc);
    }

    // Check if this is a 'let' clause:
    if (auto *E = ctx_.getLetExprByName(varName)) {
      CloneCtx map;
      return E->clone(map);
    }

    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "unknown identifier: " + varName + ".");
    return nullptr;
  }

  case l_paren: {
    consumeToken(l_paren);

    if (Expr *subExpr = parseExpr()) {
      if (!Tok.is(r_paren)) {
        ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                      "expected right paren to close the expression.");
        return nullptr;
      }
      consumeToken(r_paren);
      return subExpr;
    }
    return nullptr;
  }

  default:
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(), "unknown expression.");
    return nullptr;
  }
}

bool Parser::parseSubscriptList(std::vector<Expr *> &exprs) {
  assert(exprs.empty() && "exprs list not empty");
  if (!consumeIf(TokenKind::l_square)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting left square brace for subscript.");
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

    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting comma or end of subscript.");
    return true;
  }

  consumeToken(TokenKind::r_square);
  return false;
}

// Example: C:float<I:512,J:512>,
bool Parser::parseNamedType(Type &T, std::string &name) {
  name = Tok.getText();

  if (!consumeIf(TokenKind::identifier)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting buffer argument name");
    return true;
  }

  if (!consumeIf(TokenKind::colon)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting colon after typename: \"" + name + "\"");
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
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  std::string("expecting colon after typename \"") + name +
                      "\"");
    return true;
  }
  consumeToken();

  if (!consumeIf(TokenKind::lt)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting dimension list");
    return true;
  }

  std::vector<std::string> names;
  std::vector<unsigned> sizes;

  std::string dimName;
  int dimVal;

  // Parse the first mandatory dimension.
  if (parseTypePair(dimName, dimVal)) {
    skipUntilOneOf(TokenKind::comma, TokenKind::r_paren);
  }
  names.push_back(dimName);
  sizes.push_back(dimVal);

  // Parse the remaining optional dimensions.
  while (Tok.is(TokenKind::comma)) {
    consumeToken(TokenKind::comma);
    // Parse the first mandatory dimension.
    if (parseTypePair(dimName, dimVal)) {
      skipUntil(TokenKind::gt);
      break;
    }
    names.push_back(dimName);
    sizes.push_back(dimVal);
  }

  if (!consumeIf(TokenKind::gt)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting dimension list");
    skipUntil(TokenKind::gt);
  }

  T = Type(scalarsTy, sizes, names);
  return false;
}

bool Parser::parseScope(Scope *scope) {
  // Remember the state of the let expression stack.
  unsigned letStackHandle = ctx_.getLetStackLevel();

  if (!consumeIf(TokenKind::l_brace)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting left brace for scope body.");
  }

  while (!Tok.is(TokenKind::r_brace)) {
    // Parse the let statements at the beginning of the scope.
    while (Tok.is(TokenKind::kw_let)) {
      if (parseLetStmt()) {
        skipUntil(TokenKind::r_brace);
        goto end_scope;
      }
    }

    if (Stmt *S = parseOneStmt()) {
      scope->addStmt(S);
    } else {
      skipUntil(TokenKind::r_brace);
      goto end_scope;
    }
  }

end_scope:
  if (!consumeIf(TokenKind::r_brace)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting closing brace to scope body.");
  }

  ctx_.restoreLetStack(letStackHandle);
  return false;
}

Stmt *Parser::parseForStmt() {
  auto forLoc = Tok.getLoc();
  // "for"
  consumeToken(TokenKind::kw_for);

  // "("
  if (!consumeIf(TokenKind::l_paren)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting left paren in for loop.");
    return nullptr;
  }

  // Indentifier name.
  std::string indexName;
  if (parseIdentifier(indexName)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting index name in for loop.");
    return nullptr;
  }

  // "in" keyword.
  if (!consumeIf(TokenKind::kw_in)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting 'in' keyword in the for loop.");
    return nullptr;
  }

  int zero = 0;
  int endRange = 0;
  if (parseIntegerLiteral(zero) || zero != 0) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting '0' in the for base range. Remember "
                  "the space between the zero and '..'");
    skipUntil(TokenKind::r_paren);
    goto end_loop_decl;
  }

  // ".." range keyword.
  if (!consumeIf(TokenKind::range)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting the '..' range in the for loop. "
                  "Remember the space between the zero and '..'");
    skipUntil(TokenKind::r_paren);
    goto end_loop_decl;
  }

  if (parseLiteralOrDimExpr(endRange)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "unable to parse loop range.");
    skipUntil(TokenKind::r_paren);
    goto end_loop_decl;
  }
end_loop_decl:
  // ")"
  if (!consumeIf(TokenKind::r_paren)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting right brace in for loop.");
  }

  // Create the loop.
  Loop *L = new Loop(indexName, forLoc, endRange);

  ctx_.pushLoop(L);
  // Parse the body of the loop.
  if (parseScope(L)) {
    skipUntil(TokenKind::r_brace);
  }
  auto *K = ctx_.popLoop();
  assert(K == L && "Popped an unexpected loop");
  return L;
}

Stmt *Parser::parsePragma() {
  assert(Tok.is(TokenKind::hash));

  auto pragmaLoc = Tok.getLoc();
  consumeToken(TokenKind::hash);
  std::string pragmaName;
  int param;

  // Parse the pragma name.
  if (parseIdentifier(pragmaName)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "unable to parse the pragma name.");
    skipUntil(TokenKind::kw_for);
    goto parse_loop;
  }
  // Parse the parameter.
  if (parseIntegerLiteralOrLetConstant(param)) {
    ctx_.diagnose(
        DiagnoseKind::Error, pragmaLoc,
        "expecting a numeric pragma parameter after the pragma name.");
  }

parse_loop:
  // Continue to parse statements recursively. Apply the pragma as the loop
  // returns.
  Stmt *K = parseOneStmt();
  if (!K)
    return nullptr;

  Loop *L = dynamic_cast<Loop *>(K);

  PragmaCommand::PragmaKind pk = PragmaCommand::PragmaKind::other;

#define MATCH(str, name, kind)                                                 \
  {                                                                            \
    if (str == name) {                                                         \
      pk = kind;                                                               \
    }                                                                          \
  }
  MATCH(pragmaName, "vectorize", PragmaCommand::PragmaKind::vectorize);
  MATCH(pragmaName, "widen", PragmaCommand::PragmaKind::widen);
  MATCH(pragmaName, "tile", PragmaCommand::PragmaKind::tile);
  MATCH(pragmaName, "peel", PragmaCommand::PragmaKind::peel);
  MATCH(pragmaName, "unroll", PragmaCommand::PragmaKind::unroll);
  MATCH(pragmaName, "hoist", PragmaCommand::PragmaKind::hoist);
#undef MATCH

  if (pk == PragmaCommand::PragmaKind::other) {
    ctx_.diagnose(DiagnoseKind::Error, pragmaLoc,
                  "unknown pragma \"" + pragmaName + "\".\n");
    return nullptr;
  }
  if (L) {
    PragmaCommand pc(pk, param, L, pragmaLoc);
    ctx_.addPragma(pc);
  } else {
    ctx_.diagnose(DiagnoseKind::Error, pragmaLoc,
                  "unable to apply the pragma to non-loop.");
  }
  return L;
}

Stmt *Parser::parseIfStmt() {
  auto ifLoc = Tok.getLoc();
  // "if"
  consumeToken(TokenKind::kw_if);

  int startRange = 0;
  int endRange = 0;

  // "("
  if (!consumeIf(TokenKind::l_paren)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting left paren in for loop.");
  }

  Expr *indexVal = parseExpr();
  if (!indexVal) {
    skipUntil(TokenKind::r_paren);
    goto end_loop_decl;
  }

  // "in" keyword.
  if (!consumeIf(TokenKind::kw_in)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting 'in' keyword in the for loop.");
    skipUntil(TokenKind::r_paren);
    goto end_loop_decl;
  }

  if (parseLiteralOrDimExpr(startRange)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "unable to parse if-range.");
    skipUntil(TokenKind::r_paren);
    goto end_loop_decl;
  }

  // ".." range keyword.
  if (!consumeIf(TokenKind::range)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting the '..' range in the if-range "
                  "loop. Remember the space between the value "
                  "and '..'");
    skipUntil(TokenKind::r_paren);
    goto end_loop_decl;
  }

  if (parseLiteralOrDimExpr(endRange)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "unable to parse if-range.");
    skipUntil(TokenKind::r_paren);
  }

end_loop_decl:

  // ")"
  if (!consumeIf(TokenKind::r_paren)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting right brace in for loop.");
  }

  // Create the if-range.
  IfRange *IR = new IfRange(indexVal, startRange, endRange, ifLoc);

  // Parse the body of the loop.
  if (parseScope(IR)) {
    skipUntil(TokenKind::r_brace);
    return nullptr;
  }

  return IR;
}

bool Parser::parseLiteralOrDimExpr(int &value) {
  if (Tok.is(TokenKind::integer_literal)) {
    // End of index range.
    if (parseIntegerLiteral(value)) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "expecting an integer value.");
      return true;
    }

    return false;
  }

  if (Tok.is(identifier)) {
    std::string varName;
    if (parseIdentifier(varName)) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "expecting argument name.");
      return true;
    }

    // Is this a 'let' variable that contains an integer?
    if (auto *E = ctx_.getLetExprByName(varName)) {
      if (ConstantExpr *C = dynamic_cast<ConstantExpr *>(E)) {
        value = C->getValue();
        return false;
      }
    }

    Argument *arg = ctx_.getArgumentByName(varName);
    if (!arg) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "unexpected argument name in for loop range: " + varName);
      return true;
    }

    if (!consumeIf(TokenKind::period)) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "expecting a member access in loop range: " + varName);
      return true;
    }

    std::string dimName;
    if (parseIdentifier(dimName)) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "expecting dimension name in loop range: " + varName);
      return true;
    }

    value = arg->getType()->getDimSizeByName(dimName);
    if (value == 0) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "invalid dimension name in: " + varName + "." + dimName);
      return true;
    }

    return false;
  }

  ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                "invalid expression in dimension name.");
  return true;
}

bool Parser::parseLetStmt() {
  assert(Tok.is(TokenKind::kw_let));
  consumeToken(TokenKind::kw_let);

  std::string varName;
  // Parse the pragma name.
  if (parseIdentifier(varName)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting a variable name in 'let' expr.");
  }

  if (!consumeIf(TokenKind::assign)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting assignment in let expression.");
  }

  Expr *storedValue = parseExpr();
  if (!storedValue) {
    return true;
  }
  
  ctx_.registerLetValue(varName, storedValue);
  return false;
}

/// Parse a single unit (stmt).
Stmt *Parser::parseOneStmt() {
  // Parse pragmas such as :
  // #vectorize 8
  if (Tok.is(TokenKind::hash)) {
    return parsePragma();
  }

  /// Parse variable access:
  ///
  /// Example: A[1,I * 8, 12] += 2
  if (Tok.is(TokenKind::identifier)) {
    auto argLoc = Tok.getLoc();
    std::string varName;
    parseIdentifier(varName);

    Argument *arg = ctx_.getArgumentByName(varName);
    if (!arg) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "accessing unknown variable.");
      return nullptr;
    }

    if (Tok.is(l_square)) {
      if (!arg) {
        ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                      "unknown subscript argument " + varName + ".");
        return nullptr;
      }
      std::vector<Expr *> indices;
      if (parseSubscriptList(indices)) {
        return nullptr;
      }

      bool accumulate;

      auto asLoc = Tok.getLoc();
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
        ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                      "expecting assignment operator after buffer access.");
        return nullptr;
      }

      Expr *storedValue = parseExpr();
      if (!storedValue) {
        return nullptr;
      }

      // Check that the number of subscript arguments is correct.
      if (arg->getType()->getNumDims() != indices.size()) {
        ctx_.diagnose(DiagnoseKind::Error, argLoc,
                      "Invalid number of indices for argument subscript");
        return nullptr;
      }

      return new StoreStmt(arg, indices, storedValue, accumulate, asLoc);
    }

    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting subscript after identifier " + varName + ".");
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

  ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                "unknown statement in scope body.");
  return nullptr;
}

Program *Parser::parseFunctionDecl() {
  if (!consumeIf(kw_def)) {
    skipUntil(TokenKind::eof);
    return nullptr;
  }

  // Indentifier name.
  std::string progName = "prog";
  if (parseIdentifier(progName)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting function name after def.");
    skipUntil(TokenKind::l_paren);
  }

  Program *p = new Program(progName, Tok.getLoc());

  if (!consumeIf(TokenKind::l_paren)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting argument list after function name.");
  }

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
      skipUntil(TokenKind::comma);
    }

    if (ctx_.getArgumentByName(typeName)) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "argument with this name already exists.");
      // Try to recover by ignoring this argument.
      continue;
    }

    auto *arg = new Argument(typeName, T);
    ctx_.registerNewArgument(arg);
    p->addArgument(arg);
  }

  if (!consumeIf(TokenKind::r_paren)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting the end of the argument list.");
    skipUntil(TokenKind::l_brace);
  }

  if (parseScope(p)) {
    return nullptr;
  }

  return p;
}

void Parser::Parse() {
  // Prime the Lexer!
  consumeToken();

  // Parse let statements in the beginning of the program.
  while (Tok.is(TokenKind::kw_let)) {
    if (parseLetStmt())
      return;
  }

  // Only allow function declerations in the top-level scope.
  if (Tok.is(kw_def)) {
    if (Program *func = parseFunctionDecl()) {
      ctx_.registerProgram(func);
    }

    if (!Tok.is(TokenKind::eof)) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "expecting eof of file after function.");
    }
    return;
  }

  ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                "expecting function decleration.");
  return;
}

Program *bistra::parseProgram(const char *src) {
  ParserContext ctx(src);
  Parser P(ctx);
  P.Parse();
  return ctx.getProgram();
}
