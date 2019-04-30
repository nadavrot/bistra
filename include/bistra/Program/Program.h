#ifndef BISTRA_PROGRAM_PROGRAM_H
#define BISTRA_PROGRAM_PROGRAM_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace bistra {

struct Type;

/// An enum representing the type used by the elements of a tensor.
enum class ElemKind : unsigned char {
  Float32Ty, // 32-bit float type (float)
  Int8Ty,    // 8-bit type (int8_t)
  Int32Ty,   // 32-bit type (int32_t)
};

/// A class that represents a type of a tensor.
struct Type final {
  /// Contains the dimensions (sizes) of the tensor. Ex: [sx, sy, sz, ...].
  std::vector<unsigned> sizes_{};

  /// Specifies the element type of the tensor.
  ElemKind elementType_{ElemKind::Float32Ty};

  /// Initialize a new non-quantized type.
  Type(ElemKind elemTy, const std::vector<unsigned> &dims)
      : sizes_(dims), elementType_(elemTy) {}

  /// An empty type.
  Type() = default;

  /// \returns true if \p other is the same type.
  bool isEqual(const Type &other) const {
    // Element type must be the same.
    if (elementType_ != other.elementType_) {
      return false;
    }
    // Must have the same number of sizes.
    if (sizes_ != other.sizes_) {
      return false;
    }

    return true;
  }

  /// \returns the dimensions of the tensor.
  const std::vector<unsigned> &dims() { return sizes_; }

  /// \returns the tensor element type.
  ElemKind getElementType() const { return elementType_; }

  /// \returns the number of elements in the tensor.
  size_t size() const {
    size_t s = 1;
    for (unsigned char i = 0, e = sizes_.size(); i < e; i++) {
      s *= size_t(sizes_[i]);
    }

    return s;
  }

  /// \return the textual name of the element.
  const char *getElementName() const { return getElementName(elementType_); }

  /// \return the textual name of the element \p Ty.
  static const char *getElementName(ElemKind Ty) {
    static const char *names[] = {
        "float",
        "i8",
        "i32",
    };
    return names[(int)Ty];
  }

  /// Prints the type.
  void dump();
};

inline bool operator==(const Type &LHS, const Type &RHS) {
  return LHS.isEqual(RHS);
}

/// This struct represents an input to the program, which is a Tensor, or a
/// typed region in memory.
struct Argument final {
  /// The name of the argument.
  std::string name_;

  /// The type of the argument.
  Type type_;

  Argument(const std::string &name, const Type &t) : name_(name), type_(t) {}

  /// Prints the argument.
  void dump();
};

struct Stmt {
  /// Prints the argument.
  virtual void dump(unsigned indent) = 0;
  virtual ~Stmt() = default;
};

class Scope;

/// This class represents a program.
class Program final {
  /// \represents the list of arguments.
  std::vector<Argument> args_;

  /// Set the body of the program.
  Scope *body_;

public:
  ~Program();
  /// Argument getter.
  const std::vector<Argument> &getArgs() { return args_; }

  /// Adds a new argument;
  void addArgument(const std::string &name, std::vector<unsigned> dims,
                   ElemKind Ty);

  /// Adds a new argument;
  void addArgument(const Argument &arg);

  /// \sets the body of the program.
  void setBody(Scope *s) { body_ = s; }

  /// \returns the body of the program.
  Scope *getBody() { return body_; }

  /// Prints the program.
  void dump();
};

/// Represents a list of statements that are executed sequentially.
class Scope : public Stmt {
  /// Holds the body of the loop.
  std::vector<Stmt *> body_;

public:
  Scope(const std::vector<Stmt *> &body) : body_(body) {}
  Scope() {}

  ~Scope() {
    for (auto *s : body_) {
      delete s;
    }
  }

  /// \returns the body of the loop.
  std::vector<Stmt *> &getBody() { return body_; }

  virtual void dump(unsigned indent) override;
};

/// Represents a data-parallel loop from zero to End. The loop index can be
/// vectorized and unrolled.
struct Loop : public Stmt {
  /// The letter that represents the induction variable.
  std::string c_;

  /// Holds the body of the loop.
  Stmt *body_;

  // End index.
  unsigned end_;

  // Vectorization factor.
  unsigned vf_{1};

  Loop(std::string name, unsigned end, unsigned vf = 0)
      : c_(name), end_(end), vf_(vf) {}

  ~Loop() { delete body_; }

  /// \returns the name of the induction variable.
  const std::string &getName() { return c_; }

  /// \sets the body of the loop.
  void setBody(Stmt *s) { body_ = s; }

  /// \returns the body of the loop.
  Stmt *getBody() { return body_; }
  virtual void dump(unsigned indent) override;
};

struct Expr {
  /// Prints the argument.
  virtual void dump() = 0;
  virtual ~Expr() = default;
};

struct Index : Expr {
  // A reference to a loop (which the Index does not own).
  Loop *loop_;
  virtual void dump() override;
};

} // namespace bistra

#endif // BISTRA_PROGRAM_PROGRAM_H
