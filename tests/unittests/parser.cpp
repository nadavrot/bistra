#include "bistra/Parser/Parser.h"
#include "bistra/Parser/Lexer.h"

#include "gtest/gtest.h"

using namespace bistra;

const char *test_program = R"(
                def matmul(C:float<I:512,J:512>, A:float<I:512,K:512>, B:float<K:512,J:512>) {
                var C1 : float8
                 for (i in 0..512) {
                  for.8 (j in 0..512) {
                   C1 = ( 0.0 );
                   for (k in 0..512) {
                    C1 += (A[i,k]) * B[k,j].8;
                   }
                   C[i,j].8 += C1;
                  }
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
  P.getContext().getProgram()->dump();
  EXPECT_EQ(P.getContext().getNumErrors(), 0);
  Program *pg = P.getContext().getProgram();
  EXPECT_EQ(pg->getArgs().size(), 1);
  EXPECT_EQ(pg->getArg(0)->getName(), "C");
  EXPECT_EQ(pg->getArg(0)->getType()->getDims().size(), 2);
  EXPECT_EQ(pg->getArg(0)->getType()->getDims()[0], 512);
}
