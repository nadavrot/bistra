#include "bistra/Parser/Lexer.h"
#include "bistra/Parser/ParserContext.h"

using namespace bistra;

//===----------------------------------------------------------------------===//
// Setup and Helper Methods
//===----------------------------------------------------------------------===//

static const char *TokenNames[] = {
    "unknown",
#define KEYWORD(X) "kw_" #X,
#define BUILTIN_TYPE(X) "builtin_type_" #X,
#define PUNCTUATOR(X, Y) #X,
#define OTHER(X) #X,

#include "bistra/Parser/Tokens.def"

#undef BUILTIN_TYPE
#undef KEYWORD
#undef PUNCTUATOR
#undef OTHER
};

const char *Token::getName() const { return TokenNames[kind_]; }

Lexer::Lexer(ParserContext &ctx)
    : buffer_(ctx.getBuffer()), curPtr_(buffer_), context_(ctx) {}

void Lexer::formToken(TokenKind kind, const char *tokStart, Token &result) {
  result.setToken(kind, tokStart, curPtr_);
}

//===----------------------------------------------------------------------===//
// Lexer Subroutines
//===----------------------------------------------------------------------===//

void Lexer::skipSlashSlashComment() {
  assert(curPtr_[0] == '/' && curPtr_[-1] == '/' && "Not a // comment");
  while (1) {
    switch (*curPtr_++) {
    case '\n':
    case '\r':
      return; // If we found the end of the line, return.
    default:
      break; // Otherwise, eat other characters.
    case 0:
      // Rnd of file, return.
      --curPtr_;
      return;
    }
  }
}

void Lexer::lexIdentifier(Token &result) {
  // Match [a-zA-Z_][a-zA-Z_0-9]*
  const char *tokStart = curPtr_ - 1;
  assert((isalpha(*tokStart) || *tokStart == '_') && "Unexpected start");

  // Lex [a-zA-Z_0-9]*
  while (isalnum(*curPtr_) || *curPtr_ == '_')
    ++curPtr_;

  std::string txt(tokStart, curPtr_ - tokStart);
  TokenKind kind = TokenKind::identifier;

#define KEYWORD(X)                                                             \
  if (txt == #X)                                                               \
    kind = kw_##X;
#define BUILTIN_TYPE(X)                                                        \
  if (txt == #X)                                                               \
    kind = builtin_type_##X;
#define PUNCTUATOR(X, Y)
#define OTHER(X)

#include "bistra/Parser/Tokens.def"

#undef BUILTIN_TYPE
#undef KEYWORD
#undef PUNCTUATOR
#undef OTHER
  return formToken(kind, tokStart, result);
}

/// LexNumber - Match [0-9]+
void Lexer::lexNumber(Token &result) {
  const char *tokStart = curPtr_ - 1;
  assert(isdigit(*tokStart) || *tokStart == '-' && "Unexpected start");

  if (*curPtr_ == '-') {
    ++curPtr_;
  }

  bool seenPoint = false;
  // Lex [0-9]*
  while (isdigit(*curPtr_) || *curPtr_ == '.') {
    if (*curPtr_ == '.') {
      // If this is not the first period in the number then abort.
      if (seenPoint) {
        break;
      }
      seenPoint = true;
    }
    ++curPtr_;
  }
  // Create the float/int number tokens.
  if (seenPoint)
    return formToken(float_literal, tokStart, result);

  return formToken(integer_literal, tokStart, result);
}

//===----------------------------------------------------------------------===//
// Lexer Loop
//===----------------------------------------------------------------------===//

void Lexer::Lex(Token &result) {
Restart:
  // Remember the start of the token so we can form the text range.
  const char *tokStart = curPtr_;

  switch (*curPtr_++) {
  default:
    context_.diagnose(curPtr_, "Invalid character.");
    return formToken(unknown, tokStart, result);

  case ' ':
  case '\t':
  case '\n':
  case '\r':
  case ';':
    goto Restart; // Skip whitespace.
  case 0:
    // This is the end of the buffer.  Return EOF.
    return formToken(eof, tokStart, result);
  case '#':
    return formToken(hash, tokStart, result);
  case ',':
    return formToken(comma, tokStart, result);
  case ':':
    return formToken(colon, tokStart, result);
  case '.':
    if (curPtr_[0] == '.') {
      curPtr_++;
      return formToken(range, tokStart, result);
    }
    return formToken(period, tokStart, result);
  case '=':
    if (curPtr_[0] == '=') {
      curPtr_++;
      return formToken(equal, tokStart, result);
    }
    return formToken(assign, tokStart, result);

  case '<':
    if (curPtr_[0] == '=') {
      curPtr_++;
      return formToken(lte, tokStart, result);
    }
    return formToken(lt, tokStart, result);
  case '>':
    if (curPtr_[0] == '=') {
      curPtr_++;
      return formToken(gte, tokStart, result);
    }
    return formToken(gt, tokStart, result);

  case '!':
    if (curPtr_[0] == '=') {
      curPtr_++;
      return formToken(not_equal, tokStart, result);
    }
    return formToken(bang, tokStart, result);

  case '+':
    if (curPtr_[0] == '=') {
      curPtr_++;
      return formToken(plusEquals, tokStart, result);
    }
    return formToken(plus, tokStart, result);
  case '-':
    if (isdigit(curPtr_[0])) {
      return lexNumber(result);
    }
    return formToken(minus, tokStart, result);
  case '*':
    return formToken(mult, tokStart, result);
  case '/':
    if (curPtr_[0] == '/') {
      skipSlashSlashComment();
      goto Restart;
    }
    return formToken(div, tokStart, result);

  case '{':
    return formToken(l_brace, tokStart, result);
  case '}':
    return formToken(r_brace, tokStart, result);
  case '(':
    return formToken(l_paren, tokStart, result);
  case ')':
    return formToken(r_paren, tokStart, result);
  case '[':
    return formToken(l_square, tokStart, result);
  case ']':
    return formToken(r_square, tokStart, result);

  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'E':
  case 'F':
  case 'G':
  case 'H':
  case 'I':
  case 'J':
  case 'K':
  case 'L':
  case 'M':
  case 'N':
  case 'O':
  case 'P':
  case 'Q':
  case 'R':
  case 'S':
  case 'T':
  case 'U':
  case 'V':
  case 'W':
  case 'X':
  case 'Y':
  case 'Z':
  case 'a':
  case 'b':
  case 'c':
  case 'd':
  case 'e':
  case 'f':
  case 'g':
  case 'h':
  case 'i':
  case 'j':
  case 'k':
  case 'l':
  case 'm':
  case 'n':
  case 'o':
  case 'p':
  case 'q':
  case 'r':
  case 's':
  case 't':
  case 'u':
  case 'v':
  case 'w':
  case 'x':
  case 'y':
  case 'z':
  case '_':
    return lexIdentifier(result);

  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    return lexNumber(result);
  }
}
