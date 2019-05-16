#include "bistra/Parser/Parser.h"
#include "bistra/Parser/Lexer.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include "gtest/gtest.h"

using namespace bistra;

const char *test_program = R"(
def matmul(C:float<I:512,J:512>, A:float<I:512,K:512>, B:float<K:512,J:512>) {
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
def matmul(C:float<I:512,J:512>, A:float<I:512,K:512>, B:float<K:512,J:512>) {
  for (i in 0 .. 512) {
        C[i + 3, i * 2 ] += (A[i, (4 + 2) * i]) * B[4 + 4, 8 + (8 * i)] + 0.34;
  }
})";

TEST(basic, lexer1) {
  ParserContext ctx;
  Lexer L(ctx, "def test (1,2) // comment. ");
  Token result;

  L.Lex(result);
  EXPECT_EQ(result.getKind(), TokenKind::kw_def);
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
  ParserContext ctx;
  Parser P("def matmul(C:float<I:512,J:512>) {}", ctx);
  P.Parse();
  ctx.getProgram()->dump();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *pg = ctx.getProgram();
  EXPECT_EQ(pg->getArgs().size(), 1);
  EXPECT_EQ(pg->getArg(0)->getName(), "C");
  EXPECT_EQ(pg->getArg(0)->getType()->getDims().size(), 2);
  EXPECT_EQ(pg->getArg(0)->getType()->getDims()[0], 512);
}

TEST(basic, parse_for) {
  ParserContext ctx;
  Parser P("def matmul(C:float<I:512,J:512>) {  for (i in 0 .. 125) {} }", ctx);
  P.Parse();
  ctx.getProgram()->dump();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *pg = ctx.getProgram();
  Loop *forStmt = dynamic_cast<Loop *>(pg->getBody()[0].get());
  EXPECT_EQ(forStmt->getName(), "i");
  EXPECT_EQ(forStmt->getEnd(), 125);
}

TEST(basic, parse_whole_file) {
  ParserContext ctx;
  Parser P(test_program, ctx);
  P.Parse();
  ctx.getProgram()->dump();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *pg = ctx.getProgram();
  Loop *forStmt = dynamic_cast<Loop *>(pg->getBody()[0].get());
  EXPECT_EQ(forStmt->getName(), "i");
  EXPECT_EQ(forStmt->getEnd(), 512);
}

const char *use_buffer_index = R"(
def use_buffer_index(C:float<I:512,J:512>) {
  for (i in 0 .. C.I) {
    for (j in 0 .. C.J) {
      C [i, j ] = 0;
    }
  }
})";

TEST(basic, use_buffer_index) {
  ParserContext ctx;
  Parser P(use_buffer_index, ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *pg = ctx.getProgram();
  pg->dump();
  EXPECT_EQ(::getLoopByName(pg, "i")->getEnd(), 512);
  EXPECT_EQ(::getLoopByName(pg, "i")->getEnd(), 512);
}

TEST(basic, comperators) {
  const char *comperators = R"(
  def simple_comperator(C:float<I:10>) {
    C [0] = C[0] > C[1];
    for (i in 0 .. 10) {
      for (j in 0 .. 10) {
        C [1] = i < j;
        C [2] = i == j;
      }
    }
  })";

  ParserContext ctx;
  Parser P(comperators, ctx);
  P.Parse();
  EXPECT_EQ(ctx.getNumErrors(), 0);
  Program *pg = ctx.getProgram()->dump();
}
