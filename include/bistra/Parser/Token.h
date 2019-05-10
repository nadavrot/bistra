#ifndef BISTRA_PARSER_TOKEN_H
#define BISTRA_PARSER_TOKEN_H

#include <string>

namespace bistra {
enum TokenKind {
  unknown = 0,

#define KEYWORD(X) kw_##X,
#define BUILTIN_TYPE(X) builtin_type_##X,
#define PUNCTUATOR(X, Y) X,
#define OTHER(X) X,

#include "Tokens.def"

#undef BUILTIN_TYPE
#undef KEYWORD
#undef PUNCTUATOR
#undef OTHER

  NUM_TOKENS
};

class Token {
  /// The kind of the token.
  TokenKind kind_;

  /// The text covered by the token.
  const char *start_;
  const char *end_;

public:
  /// Initializes the token.
  void setToken(TokenKind K, const char *start, const char *end) {
    kind_ = K;
    start_ = start;
    end_ = end;
    assert(end_ > start_ && "Invalid text range");
  }
  /// Returns the kind of the token.
  TokenKind getKind() const { return kind_; }
  /// Sets the kind of the token.
  void setKind(TokenKind K) { kind_ = K; }
  /// Returns true if this token is of kind \p K.
  bool is(TokenKind K) const { return kind_ == K; }
  /// Returns the text.
  const std::string getText() const { return std::string(start_, end_); }
  /// Sets the text.
  void setText(const char *start, const char *end) {
    start_ = start;
    end_ = end;
    assert(end_ > start_ && "Invalid text range");
  }
  /// Returns the length of the text.
  unsigned getLength() const { return end_ - start_; }
  /// Return a textual description for the token.
  const char *getName() const;
};

} // end namespace bistra

#endif
