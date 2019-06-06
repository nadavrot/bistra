#include "bistra/Analysis/Value.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Parser/Lexer.h"
#include "bistra/Parser/Parser.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include "gtest/gtest.h"

using namespace bistra;

const char *test_program = R"(
func matmul(C:float<I:512,J:512>, A:float<I:512,K:512>, B:float<K:512,J:512>) {
  for (i in 0 .. 512) {
    for (j in 0 .. 512) {
      C[i,j] = 0.0;
      for (k in 0 .. 512) {
        C[i,j] += (A[i,k]) * B[k,j];
      }
    }
  }
})";

const char *test_program2 = R"(
func matmul(C:float<I:512,J:512>, A:float<I:512,K:512>, B:float<K:512,J:512>) {
  for (i in 0 .. 512) {
        C[i + 3, i * 2 ] += (A[i, (4 + 2) * i]) * B[4 + 4, 8 + (8 * i)] + 0.34;
  }
})";

TEST(basic, lexer1) {
  ParserContext ctx("func test (1,-2) // comment. ");
  Lexer L(ctx);
  Token result;

  L.Lex(result);
  EXPECT_EQ(result.getKind(), TokenKind::kw_func);
  L.Lex(result);
  EXPECT_EQ(result.getKind(), TokenKind::identifier);
  L.Lex(result);
  EXPECT_EQ(result.getKind(), TokenKind::l_paren);
  L.Lex(result);
  EXPECT_EQ(result.getKind(), TokenKind::integer_literal);
  L.Lex(result);
  EXPECT_EQ(result.getKind(), TokenKind::comma);
  L.Lex(result);
  EXPECT_EQ(result.getKind(), TokenKind::integer_literal);
  L.Lex(result);
  EXPECT_EQ(result.getKind(), TokenKind::r_paren);
  L.Lex(result);
  EXPECT_EQ(result.getKind(), TokenKind::eof);
}

TEST(basic, parse_decl) {
  ParserContext ctx("func matmul(C:float<I:512,J:512>) {}");
  Parser P(ctx);
  P.Parse();
  auto *pg = ctx.getProgram();
  pg->verify();
  pg->dump();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  EXPECT_EQ(pg->getArgs().size(), 1);
  EXPECT_EQ(pg->getArg(0)->getName(), "C");
  EXPECT_EQ(pg->getArg(0)->getType()->getDims().size(), 2);
  EXPECT_EQ(pg->getArg(0)->getType()->getDims()[0], 512);
}

TEST(basic, parse_for) {
  ParserContext ctx(
      "func matmul(C:float<I:512,J:512>) {  for (i in 0 .. 125) {} }");
  Parser P(ctx);
  P.Parse();
  auto *pg = ctx.getProgram();
  pg->verify();
  pg->dump();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Loop *forStmt = dynamic_cast<Loop *>(pg->getBody()[0].get());
  EXPECT_EQ(forStmt->getName(), "i");
  EXPECT_EQ(forStmt->getEnd(), 125);
}

TEST(basic, parse_whole_file) {
  ParserContext ctx(test_program);
  Parser P(ctx);
  P.Parse();
  auto *pg = ctx.getProgram();
  pg->verify();
  pg->dump();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Loop *forStmt = dynamic_cast<Loop *>(pg->getBody()[0].get());
  EXPECT_EQ(forStmt->getName(), "i");
  EXPECT_EQ(forStmt->getEnd(), 512);
}

const char *use_buffer_index = R"(
func use_buffer_index(C:float<I:512,J:512>) {
  for (i in 0 .. C.I) {
    for (j in 0 .. C.J) {
      C [i, j ] = 0.;
    }
  }
})";

TEST(basic, use_buffer_index) {
  ParserContext ctx(use_buffer_index);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *pg = ctx.getProgram();
  pg->verify();
  pg->dump();
  EXPECT_EQ(::getLoopByName(pg, "i")->getEnd(), 512);
  EXPECT_EQ(::getLoopByName(pg, "i")->getEnd(), 512);
}

TEST(basic, comperators) {
  const char *comperators = R"(
  func simple_comperator(C:float<I:10>) {
    C [0] = C[0] + C[1];
    for (i in 0 .. 10) {
      for (j in 0 .. 10) {
        C [1] = C[4] - C[6];
        C [2] = C[5] / C[7];
      }
    }
  })";

  ParserContext ctx(comperators);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  ctx.getProgram()->dump();
}

TEST(basic, if_range_test) {
  const char *if_range_test = R"(
  func if_range_test(C:float<x:10>) {

    for (i in 0 .. 34) {
      if (i in 0 .. C.x) {  }
    }

    if (56 in 0 .. 10) {  }
  })";

  ParserContext ctx(if_range_test);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *p = ctx.getProgram();
  p->verify();
  p->dump();
}

TEST(basic, pragmas) {
  const char *pragmas_test = R"(
  func pragmas_test(C:float<x:10>) {
    #vectorize 8
    #widen 3
    for (i in 0 .. 34) {
    #widen 4
      for (r in 0 .. C.x) {  }
    }
  })";

  ParserContext ctx(pragmas_test);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *p = ctx.getProgram();
  p->verify();
  p->dump();
  auto decls = ctx.getPragmaDecls();
  EXPECT_EQ(decls.size(), 3);
  EXPECT_EQ(decls[2].kind_, PragmaCommand::PragmaKind::vectorize);
  EXPECT_EQ(decls[2].L_->getName(), "i");
  EXPECT_EQ(decls[1].kind_, PragmaCommand::PragmaKind::widen);
  EXPECT_EQ(decls[1].L_->getName(), "i");
  EXPECT_EQ(decls[0].kind_, PragmaCommand::PragmaKind::widen);
  EXPECT_EQ(decls[0].L_->getName(), "r");
}

TEST(basic, let_expr) {
  const char *let_expr = R"(
  let width = 3.0;
  let offset = 2;

  func let_exprs(C:float<x:10>) {
    let foo = 1.0;
    let offset2 = 2;
    C[offset + offset2] = width + foo;
    for (i in 0 .. offset2) {
      let offset2 = 300; // Redefine offset2.
      for (j in 0 .. offset2) { }
    }
  })";

  ParserContext ctx(let_expr);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *p = ctx.getProgram();
  p->verify();
  p->dump();
  NodeCounter counter;
  p->visit(&counter);
  EXPECT_EQ(counter.stmt, 4);
  EXPECT_EQ(counter.expr, 6);
  // Make sure that the inner scope clobbers 'offset2'.
  EXPECT_EQ(::getLoopByName(p, "j")->getEnd(), 300);
}

TEST(basic, let_expr_type) {
  const char *let_expr_type = R"(
  let val = 2;
  func let_exprs(C:float<x:val>) { })";
  ParserContext ctx(let_expr_type);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  ctx.getProgram()->verify();
}

TEST(basic, debug_loc) {
  const char *debug_loc = R"(
  func debug_loc(C:float<x:10>) { for (i in 0 .. 10) {} })";
  ParserContext ctx(debug_loc);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *p = ctx.getProgram();
  p->verify();
  // The loop is at the 34th char.
  EXPECT_EQ(::getLoopByName(p, "i")->getLoc().getStart(), debug_loc + 35);
}

TEST(basic, var_decl) {
  const char *var_decl = R"(
  func var_decl(C:float<x:100>) {
    var xxx : float
    xxx = 4.3
  })";
  ParserContext ctx(var_decl);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *prog = P.getContext().getProgram();
  prog->verify();
  auto *xxx = prog->getVar("xxx");
  EXPECT_EQ(xxx->getType().getTypename(), "float");
}

TEST(basic, var_load_decl) {
  const char *var_load_decl = R"(
  func var_load_decl(C:float<x:100>) {
    var xxx : float = 2.3
    var res : float = 24.
    xxx = 4.3
    res = xxx + 3.
    C[0] = res
  })";
  ParserContext ctx(var_load_decl);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *prog = P.getContext().getProgram();
  prog->verify();
}

TEST(basic, parse_binary_builtin_functions) {
  const char *parse_binary_builtin_functions = R"(
  func parse_binary_builtin_functions(C:float<x:100>) {
    C[0] = max(C[1], C[2]) + min(C[3], C[4]) + pow(C[5], C[6])
  })";
  ParserContext ctx(parse_binary_builtin_functions);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *prog = P.getContext().getProgram();
  prog->verify();
}

TEST(basic, parse_unary_functions) {
  const char *parse_unary_functions = R"(
  func parse_binary_builtin_functions(C:float<x:100>) {
    C[0] = log(exp(sqrt(1.3))) + sqrt(log(C[0]) + 3.4) + abs(-2.3)
  })";
  ParserContext ctx(parse_unary_functions);
  Parser P(ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  auto *prog = P.getContext().getProgram();
  prog->verify();
}
