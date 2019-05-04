#ifndef BISTRA_PROGRAM_PROGRAM_H
#define BISTRA_PROGRAM_PROGRAM_H

#include "bistra/Program/Types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bistra {

class CloneCtx;

class Expr;
class Stmt;
class NodeVisitor;

/// This struct represents an input to the program, which is a Tensor, or a
/// typed region in memory.
class Argument final {
  /// The name of the argument.
  std::string name_;

  /// The type of the argument.
  Type type_;

public:
  Argument(const std::string &name, const Type &t) : name_(name), type_(t) {}

  /// \returns the type of the argument.
  const Type *getType() const { return &type_; }

  /// \returns the name of the argument.
  std::string getName() const { return name_; }

  /// Prints the argument.
  void dump() const;

  /// Crash if the program is in an invalid state.
  void verify() const;
};

class Stmt {
public:
  /// Prints the argument.
  virtual void dump(unsigned indent) const = 0;
  virtual ~Stmt() = default;
  /// \returns an unowned clone of the current node and updates \p map with the
  /// cloned value.
  virtual Stmt *clone(CloneCtx &map) = 0;
  /// Crash if the program is in an invalid state.
  virtual void verify() const = 0;
  /// A node visitor that visits all of the nodes in the program.
  virtual void visit(NodeVisitor *visitor) = 0;
};

class ExprHandle;

class Expr {
  friend ExprHandle;
  /// The type of the expression.
  ExprType type_;
  /// A pointer to a handle that may contain this expression.
  ExprHandle *user_{nullptr};

public:
  Expr(const ExprType &ty) : type_(ty) {}

  Expr(ElemKind &kind) : type_(ExprType(kind)) {}

  /// Replaces the handle that references this expression with \p other.
  void replaceUserWith(Expr *other);

  /// \returns the user of this expression.
  ExprHandle *getUser() { return user_; }

  /// \returns the type of the expression.
  const ExprType &getType() const { return type_; }

  /// Sets the type of the expression.
  void setType(const ExprType &ty) { type_ = ty; }

  /// Prints the argument.
  virtual void dump() const = 0;

  virtual ~Expr() = default;

  /// Clone the expression recursively and return the cloned graph. Use the map
  /// \p map to refer to the updated indices and arguments.
  virtual Expr *clone(CloneCtx &map) = 0;

  /// Crash if the program is in an invalid state.
  virtual void verify() const = 0;

  /// A node visitor that visits all of the nodes in the program.
  virtual void visit(NodeVisitor *visitor) = 0;

  Expr() = delete;
  Expr(const Expr &other) = delete;
};

class ExprHandle final {
  Expr *ref_{nullptr};

public:
  ExprHandle() = default;
  ExprHandle(Expr *ref) { set(ref); }
  ~ExprHandle() { delete ref_; }

  void set(Expr *ref) {
    // Unregister the previous expression.
    if (ref_) {
      ref_->user_ = nullptr;
    }

    // Register the new expression.
    ref_ = ref;
    if (ref_) {
      // Reset the old handle.
      if (auto *EH = ref_->getUser()) {
        EH->ref_ = nullptr;
      }
      // Register this as the new handle.
      ref_->user_ = this;
    }
    verify();
  }

  Expr *get() const {
    verify();
    return ref_;
  }

  Expr *operator->() {
    verify();
    return ref_;
  }

  const Expr *operator->() const {
    verify();
    return ref_;
  }

  void verify() const {
    assert(ref_ == nullptr ||
           ref_->getUser() == this && "The handle pointes to an unowned expr.");
  }

  operator Expr *() { return ref_; }

  ExprHandle(const ExprHandle &other) = delete;
  ExprHandle(const ExprHandle &&other) { set(other.ref_); }
  ExprHandle &operator=(ExprHandle &other) = delete;
  ExprHandle &operator=(ExprHandle &&other) {
    set(other.ref_);
    return *this;
  }
};

class Scope;

/// Represents a list of statements that are executed sequentially.
class Scope final : public Stmt {
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

  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Represents a data-parallel loop from zero to End. The loop index can be
/// vectorized and unrolled.
class Loop final : public Stmt {
  /// The letter that represents the induction variable.
  std::string indexName;

  /// Holds the body of the loop.
  Scope *body_;

  // End index.
  unsigned end_;

  // Vectorization factor.
  unsigned vf_{1};

public:
  Loop(std::string name, unsigned end, unsigned vf = 0)
      : indexName(name), body_(new Scope()), end_(end), vf_(vf) {}

  ~Loop() { delete body_; }

  /// \returns the name of the induction variable.
  const std::string &getName() const { return indexName; }

  /// \returns the end point of the loop.
  unsigned getEnd() const { return end_; }

  /// Add a statement to the end of the loop scope.
  void addStmt(Stmt *s) { body_->addStmt(s); }

  /// \sets the body of the loop.
  void setBody(Scope *s) { body_ = s; }

  /// \returns the body of the loop.
  Scope *getBody() const { return body_; }

  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
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

  /// Adds a new argument. \returns the newly created argument.
  Argument *addArgument(const std::string &name,
                        const std::vector<unsigned> &dims,
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

  /// Crash if the program is in an invalid state.
  void verify();

  /// A node visitor that visits all of the nodes in the program.
  void visit(NodeVisitor *visitor);
};

/// An expression for referencing a loop index.
class IndexExpr final : public Expr {
  // A reference to a loop (not owned by this index).
  Loop *loop_;

public:
  IndexExpr(Loop *loop) : Expr(ElemKind::IndexTy), loop_(loop) {}

  /// \returns the loop that this expression indexes.
  Loop *getLoop() const { return loop_; }

  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// A constant integer expression.
class ConstantExpr final : public Expr {
  /// The value that this constant integer represents.
  int64_t val_;

public:
  ConstantExpr(int64_t val) : Expr(ElemKind::IndexTy), val_(val) {}

  /// \returns the value stored by this constant.
  int64_t getValue() const { return val_; }

  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// A constant float expression.
class ConstantFPExpr final : public Expr {
  /// The value that this constant integer represents.
  float val_;

public:
  ConstantFPExpr(float val) : Expr(ElemKind::Float32Ty), val_(val) {}

  /// \returns the value stored by this constant.
  float getValue() const { return val_; }

  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// A binary arithmetic expression.
class BinaryExpr : public Expr {
protected:
  /// Left-hand-side of the expression.
  ExprHandle LHS_;
  /// Right-hand-side of the expression.
  ExprHandle RHS_;

public:
  BinaryExpr(Expr *LHS, Expr *RHS)
      : Expr(LHS->getType()), LHS_(LHS), RHS_(RHS) {
    assert(LHS->getType() == RHS->getType() && "Invalid expr type");
  }

  ~BinaryExpr() = default;

  Expr *getLHS() { return LHS_; }
  Expr *getRHS() { return RHS_; }
  virtual void dump() const override = 0;
  virtual Expr *clone(CloneCtx &map) override = 0;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

class AddExpr final : public BinaryExpr {
public:
  AddExpr(Expr *LHS, Expr *RHS) : BinaryExpr(LHS, RHS) {}
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
};

class MulExpr : public BinaryExpr {
public:
  MulExpr(Expr *LHS, Expr *RHS) : BinaryExpr(LHS, RHS) {}
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
};

/// Loads some value from a buffer.
class LoadExpr final : public Expr {
  /// The buffer to access.
  Argument *arg_;
  /// The indices for indexing the buffer.
  std::vector<ExprHandle> indices_;

public:
  /// \returns the buffer destination of the instruction.
  Argument *getDest() { return arg_; }

  /// \returns the indices indexing into the array.
  const std::vector<ExprHandle> &getIndices() { return indices_; }

  LoadExpr(Argument *arg, const std::vector<Expr *> &indices)
      : Expr(ElemKind::IndexTy), arg_(arg), indices_() {
    for (auto *E : indices) {
      assert(E->getType().isIndexTy() && "Argument must be of index kind");
      indices_.emplace_back(E);
    }
    assert(arg->getType()->getNumDims() == indices_.size() &&
           "Invalid number of indices");
    // Get the element kind.
    ElemKind EK = arg->getType()->getElementType();
    // Get the vectorization factor.
    unsigned FV = indices[indices.size() - 1]->getType().getWidth();
    setType(ExprType(EK, FV));
  }

  ~LoadExpr() = default;

  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Stores some value to a buffer.
class StoreStmt final : public Stmt {
  /// The buffer to access.
  Argument *arg_;
  /// The indices for indexing the buffer.
  std::vector<ExprHandle> indices_;
  /// The value to store into the buffer.
  ExprHandle value_;
  /// Accumulate the resule into the destination.
  bool accumulate_;

public:
  /// \returns the write destination of the store instruction.
  Argument *getDest() { return arg_; }

  /// \returns the indices indexing into the array.
  const std::vector<ExprHandle> &getIndices() { return indices_; }

  /// \returns the stored value.
  ExprHandle &getValue() { return value_; }

  /// \returns true if this store statement accumulates into the stored
  /// destination.
  bool isAccumulate() { return accumulate_; }

  StoreStmt(Argument *arg, const std::vector<Expr *> &indices, Expr *value,
            bool accumulate)
      : arg_(arg), indices_(), value_(value), accumulate_(accumulate) {
    assert(arg->getType()->getNumDims() == indices.size() &&
           "Invalid number of indices");
    for (auto *E : indices) {
      assert(E->getType().isIndexTy() && "Argument must be of index kind");
      indices_.emplace_back(E);
    }
  }

  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

class CloneCtx final {
  // Maps arguments.
  std::unordered_map<Argument *, Argument *> args;
  // Maps loops.
  std::unordered_map<Loop *, Loop *> loops;

public:
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
