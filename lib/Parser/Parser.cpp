#include "bistra/Parser/Parser.h"
#include "bistra/Parser/Lexer.h"
#include "bistra/Parser/ParserContext.h"
#include "bistra/Program/Pragma.h"
#include "bistra/Program/Program.h"
#include "bistra/Transforms/Simplify.h"

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

bool Parser::parseStringLiteral(std::string &val) {
  if (Tok.is(TokenKind::string_literal)) {
    val = Tok.getText().c_str();
    consumeToken(TokenKind::string_literal);
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
    if (auto *E = ctx_.getLetStack().getByName(varName)) {
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
using ExprPtr = Expr *;
static void tryToAdjustTypes(ExprPtr &LHS, ExprPtr &RHS) {
  auto LT = LHS->getType();
  auto RT = RHS->getType();
  if (LT == RT)
    return;

  // Try to broadcast RHS.
  if (LT.getWidth() > 0 && RT.getWidth() == 1) {
    RHS = new BroadcastExpr(RHS, LT.getWidth());
    return;
  }
  // Try to broadcast LHS.
  if (RT.getWidth() > 0 && LT.getWidth() == 1) {
    LHS = new BroadcastExpr(LHS, RT.getWidth());
    return;
  }
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
    tryToAdjustTypes(LHS, RHS);                                                \
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

bool Parser::parseCallArgumentList(std::vector<Expr *> &args, bool sameTy,
                                   int expectedArgs) {
  assert(args.size() == 0);
  // "("
  if (!consumeIf(TokenKind::l_paren)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting left paren in argument list.");
    return true;
  }

  // Parse the first mandatory argument.
  if (auto *E = parseExpr()) {
    args.push_back(E);
  } else {
    skipUntilOneOf(r_paren, r_brace);
    return true;
  }

  // Parse the remaining optional dimensions.
  while (Tok.is(TokenKind::comma)) {
    consumeToken(TokenKind::comma);
    if (auto *E = parseExpr()) {
      args.push_back(E);
      continue;
    } else {
      skipUntilOneOf(r_paren, r_brace);
      return true;
    }
  }

  if (!consumeIf(TokenKind::r_paren)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting right paren in argument list");
    skipUntil(TokenKind::gt);
  }

  // Verify that we are passing the correct number of arguments.
  if (expectedArgs && expectedArgs != args.size()) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting " + std::to_string(expectedArgs) + " arguments");
    return true;
  }

  // Verify that all of the arguments are of the same type.
  if (sameTy) {
    for (auto *E : args) {
      if (!E->getType().isEqual(args[0]->getType())) {
        ctx_.diagnose(DiagnoseKind::Error, E->getLoc(),
                      "passing arguments of different types");
        return true;
      }
    }
  }

  return false;
}

/// Unescape a c string. Translate '\\n' to '\n', etc.
static std::string unescapeCString(const std::string &s) {
  std::string res;
  std::string::const_iterator it = s.begin();
  while (it != s.end()) {
    char c = *it++;
    if (c == '\\' && it != s.end()) {
      switch (*it++) {
      case '\\':
        c = '\\';
        break;
      case 'n':
        c = '\n';
        break;
      case 't':
        c = '\t';
        break;
      default:
        // Unhandled escape sequence.
        continue;
      }
    }
    res += c;
  }

  return res;
}

Expr *Parser::parseBuiltinFunction() {
  std::vector<Expr *> args;
  auto loc = Tok.getLoc();
  auto kind = Tok.getKind();
  consumeToken();

  switch (kind) {
  case builtin_func_max: {
    if (parseCallArgumentList(args, true, 2))
      return nullptr;
    return new BinaryExpr(args[0], args[1], BinaryExpr::BinOpKind::Max, loc);
  }
  case builtin_func_min: {
    if (parseCallArgumentList(args, true, 2))
      return nullptr;
    return new BinaryExpr(args[0], args[1], BinaryExpr::BinOpKind::Min, loc);
  }
  case builtin_func_pow: {
    if (parseCallArgumentList(args, true, 2))
      return nullptr;
    return new BinaryExpr(args[0], args[1], BinaryExpr::BinOpKind::Pow, loc);
  }

  case builtin_func_log: {
    if (parseCallArgumentList(args, false, 1))
      return nullptr;
    return new UnaryExpr(args[0], UnaryExpr::UnaryOpKind::Log, loc);
  }
  case builtin_func_exp: {
    if (parseCallArgumentList(args, false, 1))
      return nullptr;
    return new UnaryExpr(args[0], UnaryExpr::UnaryOpKind::Exp, loc);
  }
  case builtin_func_sqrt: {
    if (parseCallArgumentList(args, false, 1))
      return nullptr;
    return new UnaryExpr(args[0], UnaryExpr::UnaryOpKind::Sqrt, loc);
  }

  case builtin_func_abs: {
    if (parseCallArgumentList(args, false, 1))
      return nullptr;
    return new UnaryExpr(args[0], UnaryExpr::UnaryOpKind::Abs, loc);
  }

  default: {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "Unable to parse built-in function");
    return nullptr;
  }
  } // Switch-kind.
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

  case string_literal: {
    std::string str;
    parseStringLiteral(str);
    return new ConstantStringExpr(unescapeCString(str));
  }

  case identifier: {
    auto argLoc = Tok.getLoc();
    std::string varName;
    parseIdentifier(varName);

    // Check if this identifier is a loop index.
    if (Loop *L = ctx_.getLoopByName(varName)) {
      return new IndexExpr(L, argLoc);
    }

    // Check if this is a local variable load.
    if (LocalVar *LV = ctx_.getVarMap().getByName(varName)) {
      return new LoadLocalExpr(LV, argLoc);
    }

    // Check if this is a buffer access.
    Argument *A = ctx_.getArgMap().getByName(varName);
    if (Tok.is(l_square)) {
      if (!A) {
        ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                      "unknown subscript argument " + varName + ".");
        return nullptr;
      }

      std::vector<Expr *> exprs;
      if (parseSubscriptList(exprs, TokenKind::l_square, TokenKind::r_square)) {
        return nullptr;
      }

      // Check that the number of subscript arguments is correct.
      if (A->getType()->getNumDims() != exprs.size()) {
        ctx_.diagnose(DiagnoseKind::Error, argLoc,
                      "Invalid number of indices for buffer subscript.");
        return nullptr;
      }

      // Load one element.
      int loadWidth = 1;

      // Parse the vector extension. For example: " = A[i].8"
      if (consumeIf(TokenKind::period)) {
        auto periodLoc = Tok.getLoc();
        if (parseIntegerLiteralOrLetConstant(loadWidth)) {
          ctx_.diagnose(DiagnoseKind::Error, periodLoc,
                        "expecting vector width.");
        }
      }

      ExprType ty(A->getType()->getElementType(), loadWidth);
      return new LoadExpr(A, exprs, ty, argLoc);
    }

    // Check if this is a 'let' clause:
    if (auto *E = ctx_.getLetStack().getByName(varName)) {
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

  case builtin_func_min:
  case builtin_func_max:
  case builtin_func_pow:
  case builtin_func_log:
  case builtin_func_exp:
  case builtin_func_sqrt:
  case builtin_func_abs:
    return parseBuiltinFunction();

  default:
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(), "unknown expression.");
    return nullptr;
  }
}

bool Parser::parseSubscriptList(std::vector<Expr *> &exprs, TokenKind L,
                                TokenKind R) {
  assert(exprs.empty() && "exprs list not empty");
  if (!consumeIf(L)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting open brace for parameter list.");
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
    if (Tok.is(R))
      break;

    if (Tok.is(TokenKind::comma)) {
      consumeToken(TokenKind::comma);
      continue;
    }

    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting comma or end of parameter list.");
    return true;
  }

  consumeToken(R);
  return false;
}

bool Parser::parseBuiltinType(ElemKind &kind) {
  switch (Tok.getKind()) {
  case TokenKind::builtin_type_float:
    kind = ElemKind::Float32Ty;
    break;

  case TokenKind::builtin_type_int8:
    kind = ElemKind::Int8Ty;
    break;

  case TokenKind::builtin_type_index:
    kind = ElemKind::IndexTy;
    break;

  default:
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(), "expecting typename");
    return true;
  }
  consumeToken();
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
  if (parseBuiltinType(scalarsTy)) {
    return true;
  }

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
  auto letStackHandle = ctx_.getLetStack().getStackLevel();

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

    // Parse the var declerations at the beginning of the scope.
    while (Tok.is(TokenKind::kw_var)) {
      if (parseVarDecl(scope)) {
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

  ctx_.getLetStack().restoreStack(letStackHandle);
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

  int stride = 1;
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

  // Parse the stride argument.
  if (consumeIf(TokenKind::comma)) {
    auto strideLoc = Tok.getLoc();
    if (parseIntegerLiteralOrLetConstant(stride)) {
      ctx_.diagnose(DiagnoseKind::Error, strideLoc,
                    "expecting stride parameter.");
    }
  }

end_loop_decl:
  // ")"
  if (!consumeIf(TokenKind::r_paren)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting right brace in for loop.");
  }

  if (endRange % stride) {
    ctx_.diagnose(DiagnoseKind::Error, forLoc,
                  "loop stride must divide the loop range");
    return nullptr;
  }

  // Create the loop.
  Loop *L = new Loop(indexName, forLoc, endRange, stride);

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
                  "expecting right brace in if-range.");
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
    if (auto *E = ctx_.getLetStack().getByName(varName)) {
      if (ConstantExpr *C = dynamic_cast<ConstantExpr *>(E)) {
        value = C->getValue();
        return false;
      }
    }

    Argument *arg = ctx_.getArgMap().getByName(varName);
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
                "invalid expression in dimension name");
  return true;
}

bool Parser::parseLetStmt() {
  consumeToken(TokenKind::kw_let);

  std::string varName;
  // Parse the variable name.
  if (parseIdentifier(varName)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting a variable name in 'let' expr");
  }

  if (!consumeIf(TokenKind::assign)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting assignment in let expression");
  }

  Expr *storedValue = parseExpr();
  if (!storedValue) {
    return true;
  }

  // Simplify the expression before saving it.
  storedValue = simplifyExpr(storedValue);

  ctx_.getLetStack().registerValue(varName, storedValue);
  return false;
}

bool Parser::parseVarDecl(Scope *s) {
  consumeToken(TokenKind::kw_var);

  std::string varName;
  // Parse the variable name.
  if (parseIdentifier(varName)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting a variable name in var decleration");
  }

  if (!consumeIf(TokenKind::colon)) {
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "expecting colon in var expression");
  }

  ElemKind scalarsTy;
  if (parseBuiltinType(scalarsTy)) {
    return true;
  }

  Expr *storedValue = nullptr;
  // Parse the assignment to the variable.
  if (consumeIf(TokenKind::assign)) {
    storedValue = parseExpr();
  }

  if (auto *LV = ctx_.getVarMap().getByName(varName)) {
    // TODO: add diagnostics for the location of the other var.
    ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                  "variable with this name already exists");
    return true;
  }

  auto *var = new LocalVar(varName, ExprType(scalarsTy));

  ctx_.getVarMap().registerValue(var);

  ctx_.getVarStack().registerValue(varName, var);

  // If the variable was initialized then store the value into a variable at the
  // right place in the scope.
  if (storedValue) {
    auto *init =
        new StoreLocalStmt(var, storedValue, false, storedValue->getLoc());
    s->addStmt(init);
  }

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

    /// Parse variable assignment.
    if (auto *var = ctx_.getVarStack().getByName(varName)) {
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
        ctx_.diagnose(
            DiagnoseKind::Error, Tok.getLoc(),
            "expecting assignment operator after local variable access.");
        return nullptr;
      }

      Expr *storedValue = parseExpr();
      if (!storedValue) {
        return nullptr;
      }

      // Check that the number of subscript arguments is correct.
      if (!var->getType().isEqual(storedValue->getType())) {
        ctx_.diagnose(DiagnoseKind::Error, argLoc, "Invalid assignment type");
        return nullptr;
      }

      return new StoreLocalStmt(var, storedValue, accumulate, asLoc);
    }

    // Parse function calls.
    if (Tok.is(l_paren)) {
      std::vector<Expr *> params;
      if (parseSubscriptList(params, TokenKind::l_paren, TokenKind::r_paren))
        return nullptr;

      return new CallStmt(varName, params, argLoc);
    }

    Argument *arg = ctx_.getArgMap().getByName(varName);
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
      if (parseSubscriptList(indices, TokenKind::l_square,
                             TokenKind::r_square)) {
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
  if (!consumeIf(kw_func)) {
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
  ctx_.getArgMap().registerValue(firstArg);

  // Parse the optional argument list.
  while (Tok.is(TokenKind::comma)) {
    consumeToken(TokenKind::comma);
    if (parseNamedType(T, typeName)) {
      skipUntil(TokenKind::comma);
    }

    if (ctx_.getArgMap().getByName(typeName)) {
      ctx_.diagnose(DiagnoseKind::Error, Tok.getLoc(),
                    "argument with this name already exists.");
      // Try to recover by ignoring this argument.
      continue;
    }

    auto *arg = new Argument(typeName, T);
    ctx_.getArgMap().registerValue(arg);
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

  // Register all of the variables that were declared.
  for (auto *v : ctx_.getVarMap()) {
    p->addVar(v);
  }
  return p;
}

void Parser::parse() {
  // Prime the Lexer!
  consumeToken();

  // Parse let statements in the beginning of the program.
  while (Tok.is(TokenKind::kw_let)) {
    if (parseLetStmt())
      return;
  }

  // Only allow function declerations in the top-level scope.
  if (Tok.is(kw_func)) {
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
  P.parse();
  return ctx.getProgram();
}
