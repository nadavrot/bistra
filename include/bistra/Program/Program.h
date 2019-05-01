#ifndef BISTRA_PROGRAM_PROGRAM_H
#define BISTRA_PROGRAM_PROGRAM_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bistra {

struct Type;

/// An enum representing the type used by the elements of a tensor.
enum class ElemKind : unsigned char {
  Float32Ty, // 32-bit float type (float)
  Int8Ty,    // 8-bit type (int8_t)
  IndexTy,   // The type of an index.
};

/// A class that represents a type of a tensor.
struct Type final {
  /// Contains the dimensions (sizes) of the tensor. Ex: [sx, sy, sz, ...].
  std::vector<unsigned> sizes_{};

  /// Contains the names of the dimensions.
  std::vector<std::string> names_{};

  /// Specifies the element type of the tensor.
  ElemKind elementType_{ElemKind::Float32Ty};

  /// Initialize a new non-quantized type.
  Type(ElemKind elemTy, const std::vector<unsigned> &dims,
       const std::vector<std::string> &names)
      : sizes_(dims), names_(names), elementType_(elemTy) {
    assert(names_.size() == sizes_.size() && "Invalid number of dims");
  }

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

  /// \returns the number of dimensions.
  unsigned getNumDims() { return sizes_.size(); }

  /// \returns the dimensions of the tensor.
  const std::vector<unsigned> &getDims() { return sizes_; }

  /// \returns the names of the dimensions.
  const std::vector<std::string> &getNames() { return names_; }

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

  /// \returns true if this type is an index/pointer type.
  bool isIndexTy() { return elementType_ == ElemKind::IndexTy; }

  /// \return the textual name of the element.
  const char *getElementName() const { return getElementName(elementType_); }

  /// \return the textual name of the element \p Ty.
  static const char *getElementName(ElemKind Ty) {
    static const char *names[] = {
        "float",
        "i8",
        "idx_t",
    };
    return names[(int)Ty];
  }

  /// Prints the type.
  void dump();
};

/// A class that represents a type of an element.
struct ExprType final {
  /// Specifies the element type of the tensor.
  ElemKind elementType_{ElemKind::Float32Ty};

  /// Specifies the vector width.
  unsigned width_;

  ExprType(ElemKind elemTy, unsigned width = 1)
      : elementType_(elemTy), width_(width) {
    assert(width > 0 && width < 64 && "Invalid vector width");
  }

  /// \returns true if this type is an index/pointer type.
  bool isIndexTy() { return elementType_ == ElemKind::IndexTy; }

  /// \returns true if \p other is the same type.
  bool isEqual(const ExprType &other) const {
    // Element type must be the same.
    if (elementType_ != other.elementType_) {
      return false;
    }
    // Must have the same vector width.
    if (width_ != other.width_) {
      return false;
    }

    return true;
  }

  /// \returns the number of dimensions.
  unsigned getWidth() { return width_; }

  /// \returns the tensor element type.
  ElemKind getElementType() const { return elementType_; }

  /// \return the textual name of the element.
  const char *getElementName() const {
    return Type::getElementName(elementType_);
  }

  /// Prints the type.
  void dump();
};

inline bool operator==(const Type &LHS, const Type &RHS) {
  return LHS.isEqual(RHS);
}

inline bool operator==(const ExprType &LHS, const ExprType &RHS) {
  return LHS.isEqual(RHS);
}

struct CloneCtx;

/// This struct represents an input to the program, which is a Tensor, or a
/// typed region in memory.
struct Argument final {
  /// The name of the argument.
  std::string name_;

  /// The type of the argument.
  Type type_;

  Argument(const std::string &name, const Type &t) : name_(name), type_(t) {}

  /// \returns the type of the argument.
  Type *getType() { return &type_; }

  /// \returns the name of the argument.
  std::string getName() { return name_; }

  /// Prints the argument.
  void dump();
};

struct Stmt {
  /// Prints the argument.
  virtual void dump(unsigned indent) = 0;
  virtual ~Stmt() = default;
  /// \returns an unowned clone of the current node and updates \p map with the
  /// cloned value.
  virtual Stmt *clone(CloneCtx &map) = 0;
};

class Scope;

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

  /// Add a statement to the end of the scope.
  void addStmt(Stmt *s) { body_.push_back(s); }

  /// \returns the body of the loop.
  std::vector<Stmt *> &getBody() { return body_; }

  virtual void dump(unsigned indent) override;
  virtual Stmt *clone(CloneCtx &map) override;
};

/// Represents a data-parallel loop from zero to End. The loop index can be
/// vectorized and unrolled.
struct Loop : public Stmt {
  /// The letter that represents the induction variable.
  std::string indexName;

  /// Holds the body of the loop.
  Scope *body_;

  // End index.
  unsigned end_;

  // Vectorization factor.
  unsigned vf_{1};

  Loop(std::string name, unsigned end, unsigned vf = 0)
      : indexName(name), body_(new Scope()), end_(end), vf_(vf) {}

  ~Loop() { delete body_; }

  /// \returns the name of the induction variable.
  const std::string &getName() { return indexName; }

  /// Add a statement to the end of the loop scope.
  void addStmt(Stmt *s) { body_->addStmt(s); }

  /// \sets the body of the loop.
  void setBody(Scope *s) { body_ = s; }

  /// \returns the body of the loop.
  Scope *getBody() { return body_; }

  virtual void dump(unsigned indent) override;
  virtual Stmt *clone(CloneCtx &map) override;
};

/// This class represents a program.
class Program final {
  /// \represents the list of arguments.
  std::vector<Argument *> args_;

  /// Set the body of the program.
  Scope *body_;

public:
  ~Program();

  Program();

  /// Construct a new program with the body \p body and arguments \p args.
  Program(Scope *body, const std::vector<Argument *> &args);

  /// Argument getter.
  std::vector<Argument *> &getArgs() { return args_; }

  /// \returns the n'th argument.
  Argument *getArg(unsigned idx) {
    assert(idx < args_.size() && "Invalid arg index");
    return args_[idx];
  }

  /// Adds a new argument;
  void addArgument(const std::string &name, const std::vector<unsigned> &dims,
                   const std::vector<std::string> &names, ElemKind Ty);

  /// Adds a new argument;
  void addArgument(Argument *arg);

  /// Add a statement to the end of the program scope.
  void addStmt(Stmt *s) { body_->addStmt(s); }

  /// \sets the body of the program.
  void setBody(Scope *s) { body_ = s; }

  /// \returns the body of the program.
  Scope *getBody() { return body_; }

  /// Prints the program.
  void dump();

  Program *clone();
  Program *clone(CloneCtx &map);
};

struct Expr {
  ExprType type_;

  Expr(const ExprType &ty) : type_(ty) {}

  Expr(ElemKind &kind) : type_(ExprType(kind)) {}

  Expr() = delete;
  Expr(const Expr &other) = delete;

  /// \returns the type of the expression.
  ExprType &getType() { return type_; }

  /// Sets the type of the expression.
  void setType(const ExprType &ty) { type_ = ty; }

  /// Prints the argument.
  virtual void dump() = 0;

  virtual ~Expr() = default;

  /// Clone the expression recursively and return the cloned graph. Use the map
  /// \p map to refer to the updated indices and arguments.
  virtual Expr *clone(CloneCtx &map) = 0;
};

/// An expression for referencing a loop index.
struct IndexExpr : Expr {
  // A reference to a loop (not owned by this index).
  Loop *loop_;

  IndexExpr(Loop *loop) : Expr(ElemKind::IndexTy), loop_(loop) {}

  virtual void dump() override;
  virtual Expr *clone(CloneCtx &map) override;
};

/// A constant integer expression.
struct ConstantExpr : Expr {
  /// The value that this constant integer represents.
  int64_t val_;

  ConstantExpr(int64_t val) : Expr(ElemKind::IndexTy), val_(val) {}

  virtual void dump() override;
  virtual Expr *clone(CloneCtx &map) override;
};

/// A binary arithmetic expression.
struct BinaryExpr : Expr {
  /// Left-hand-side of the expression.
  Expr *LHS_;
  /// Right-hand-side of the expression.
  Expr *RHS_;

  BinaryExpr(Expr *LHS, Expr *RHS)
      : Expr(LHS->getType()), LHS_(LHS), RHS_(RHS) {
    assert(LHS->getType() == RHS->getType() && "Invalid expr type");
  }

  ~BinaryExpr() {
    delete LHS_;
    delete RHS_;
  }

  virtual void dump() override = 0;
  virtual Expr *clone(CloneCtx &map) override = 0;
};

struct AddExpr : BinaryExpr {
  AddExpr(Expr *LHS, Expr *RHS) : BinaryExpr(LHS, RHS) {}
  virtual void dump() override;
  virtual Expr *clone(CloneCtx &map) override;
};

struct MulExpr : BinaryExpr {
  MulExpr(Expr *LHS, Expr *RHS) : BinaryExpr(LHS, RHS) {}
  virtual void dump() override;
  virtual Expr *clone(CloneCtx &map) override;
};

/// Loads some value from a buffer.
struct LoadExpr : Expr {
  /// The buffer to access.
  Argument *arg_;
  /// The indices for indexing the buffer.
  std::vector<Expr *> indices_;

  LoadExpr(Argument *arg, const std::vector<Expr *> &indices)
      : Expr(ElemKind::IndexTy), arg_(arg), indices_(indices) {
    for (auto E : indices) {
      assert(E->getType().isIndexTy() && "Argument must be of index kind");
    }
    assert(arg->getType()->getNumDims() == indices_.size() &&
           "Invalid number of indices");
    // Get the element kind.
    ElemKind EK = arg->getType()->getElementType();
    // Get the vectorization factor.
    unsigned FV = indices[indices.size() - 1]->getType().getWidth();
    setType(ExprType(EK, FV));
  }

  ~LoadExpr() {
    for (auto *exp : indices_) {
      delete exp;
    }
  }

  virtual void dump() override;
  virtual Expr *clone(CloneCtx &map) override;
};

/// Stores some value to a buffer.
struct StoreStmt : Stmt {
  /// The buffer to access.
  Argument *arg_;
  /// The indices for indexing the buffer.
  std::vector<Expr *> indices_;
  /// The value to store into the buffer.
  Expr *value_;
  /// Accumulate the resule into the destination.
  bool accumulate_;

  StoreStmt(Argument *arg, const std::vector<Expr *> &indices, Expr *value,
            bool accumulate)
      : arg_(arg), indices_(indices), value_(value), accumulate_(accumulate) {
    assert(arg->getType()->getNumDims() == indices_.size() &&
           "Invalid number of indices");
  }

  StoreStmt() {
    delete value_;
    for (auto *exp : indices_) {
      delete exp;
    }
  }

  virtual void dump(unsigned indent) override;
  virtual Stmt *clone(CloneCtx &map) override;
};

struct CloneCtx {
  // Maps arguments.
  std::unordered_map<Argument *, Argument *> args;
  // Maps loops.
  std::unordered_map<Loop *, Loop *> loops;

  /// Maps \p From to \p To and returns \p To.
  Loop *map(Loop *from, Loop *to) {
    assert(loops.count(from) == 0 && "Loop already in map");
    loops[from] = to;
    return to;
  }
  /// Maps \p From to \p To and returns \p To.
  Argument *map(Argument *from, Argument *to) {
    assert(args.count(from) == 0 && "Argument already in map");
    args[from] = to;
    return to;
  }

  /// \returns the value that \p from is mapped to. \p from must be in the map.
  Loop *get(Loop *from) {
    assert(loops.count(from) && "Loop is not in map");
    return loops[from];
  }
  /// \returns the value that \p from is mapped to. \p from must be in the map.
  Argument *get(Argument *from) {
    assert(args.count(from) && "Arg is not in map");
    return args[from];
  }
};

} // namespace bistra

#endif // BISTRA_PROGRAM_PROGRAM_H
