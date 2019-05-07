#ifndef BISTRA_PROGRAM_PROGRAM_H
#define BISTRA_PROGRAM_PROGRAM_H

#include "bistra/Program/Types.h"
#include "bistra/Program/UseDef.h"

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

class ASTNode {
public:
  /// \returns the parent expression that holds the node of this expression.
  virtual ASTNode *getParent() const = 0;
  /// Crash if the program is in an invalid state.
  virtual void verify() const = 0;
  /// A node visitor that visits all of the nodes in the program.
  virtual void visit(NodeVisitor *visitor) = 0;
};

using ExprHandle = ASTHandle<Expr, ASTNode>;
using StmtHandle = ASTHandle<Stmt, ASTNode>;

class Stmt : public ASTNode {
  /// A nullable pointer to the handle that may contain this statement.
  StmtHandle *user_{nullptr};

public:
  /// Prints the argument.
  virtual void dump(unsigned indent) const = 0;
  virtual ~Stmt() = default;
  /// \returns an unowned clone of the current node and updates \p map with the
  /// cloned value.
  virtual Stmt *clone(CloneCtx &map) = 0;

  /// \returns the use handle of this expression.
  StmtHandle *getOwnerHandle() const { return user_; }

  /// \reset the pointer to the owning handle.
  void resetOwnerHandle(StmtHandle *handle = nullptr) { user_ = handle; }

  ASTNode *getParent() const override;
};

class Expr : public ASTNode {
  /// The type of the expression.
  ExprType type_;
  /// A nullable pointer to the handle that may contain this expression.
  ExprHandle *user_{nullptr};

public:
  Expr(const ExprType &ty) : type_(ty) {}

  Expr(ElemKind &kind) : type_(ExprType(kind)) {}

  /// Replaces the handle that references this expression with \p other.
  /// Delete this expression since no one is using it.
  void replaceUseWith(Expr *other);

  /// \returns the use handle of this expression.
  ExprHandle *getOwnerHandle() const { return user_; }

  /// \reset the pointer to the owning handle.
  void resetOwnerHandle(ExprHandle *handle = nullptr) { user_ = handle; }

  ASTNode *getParent() const override;

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

  Expr() = delete;
  Expr(const Expr &other) = delete;
};

/// Represents a list of statements that are executed sequentially.
class Scope : public Stmt {
protected:
  /// Holds the body of the loop.
  std::vector<StmtHandle> body_;

public:
  Scope(const std::vector<Stmt *> &body) : body_() {
    for (auto *stmt : body) {
      body_.emplace_back(stmt, this);
    }
  }

  Scope() = default;

  /// Empties the body of the scope.
  void clear();

  /// Moves the statements from \p other to this scope.
  void takeContent(Scope *other);

  /// Add a statement to the end of the scope.
  void addStmt(Stmt *s);

  /// \remove the statement \p s, if it is in the body.
  void removeStmt(Stmt *s);

  /// Insert the statement \p s before \p where. \p where must be in the body.
  void insertBeforeStmt(Stmt *s, Stmt *where);

  /// Insert the statement \p s after \p where. \p where must be in the body.
  void insertAfterStmt(Stmt *s, Stmt *where);

  /// \returns the body of the loop.
  std::vector<StmtHandle> &getBody() { return body_; }

  virtual void dump(unsigned indent) const override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Represents a data-parallel loop from zero to End. The loop index can be
/// vectorized and unrolled.
class Loop final : public Scope {
  /// The letter that represents the induction variable.
  std::string indexName_;

  // End index.
  unsigned end_;

  // Vectorization factor.
  unsigned stride_{1};

public:
  Loop(std::string name, unsigned end, unsigned stride = 1)
      : indexName_(name), end_(end), stride_(stride) {}

  /// \returns the name of the induction variable.
  const std::string &getName() const { return indexName_; }

  /// \returns the name of the induction variable.
  void setName(const std::string &name) { indexName_ = name; }

  /// \returns the end point of the loop.
  unsigned getEnd() const { return end_; }

  /// Sets the trip count;
  void setEnd(unsigned tc) { end_ = tc; }

  /// \returns the loop stride factor.
  unsigned getStride() const { return stride_; }

  /// Updated the loop stride.
  void setStride(unsigned s) { stride_ = s; }

  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// This class represents a program.
class Program final : public Scope {
  /// \represents the list of arguments.
  std::vector<Argument *> args_;

public:
  ~Program();

  Program() = default;

  /// Construct a new program with the body \p body and arguments \p args.
  Program(const std::vector<Stmt *> &body, const std::vector<Argument *> &args);

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

  Program *clone();
  void dump() const { dump(0); }
  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// An expression for referencing a loop index.
class IndexExpr final : public Expr {
  // A reference to a loop (not owned by this index).
  Loop *loop_;

public:
  IndexExpr(Loop *loop) : Expr(ElemKind::IndexTy), loop_(loop) {}

  IndexExpr(Loop *loop, const ExprType &ty) : Expr(ty), loop_(loop) {}

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
      : Expr(LHS->getType()), LHS_(LHS, this), RHS_(RHS, this) {
    assert(LHS->getType() == RHS->getType() && "Invalid expr type");
    assert(LHS != RHS && "Invalid ownership of operands");
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

/// Broadcasts a value from scalar to vector.
class BroadcastExpr : public Expr {
  /// The value to broadcast.
  ExprHandle val_;
  /// The vectorization width.
  unsigned vf_;

public:
  BroadcastExpr(Expr *val, unsigned vf)
      : Expr(val->getType().asVector(vf)), val_(val, this), vf_(vf) {}

  Expr *getValue() { return val_; }

  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
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
      indices_.emplace_back(E, this);
    }
    assert(arg->getType()->getNumDims() == indices_.size() &&
           "Invalid number of indices");
    // This loads a scalar value from the buffer.
    setType(ExprType(arg->getType()->getElementType()));
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

  /// \returns the expression of the last index.
  const ExprHandle &getLastIndex() const {
    return indices_[indices_.size() - 1];
  }

  /// \returns the expression of the last index.
  ExprHandle &getLastIndex() { return indices_[indices_.size() - 1]; }

  /// \returns the stored value.
  ExprHandle &getValue() { return value_; }

  /// \returns true if this store statement accumulates into the stored
  /// destination.
  bool isAccumulate() { return accumulate_; }

  StoreStmt(Argument *arg, const std::vector<Expr *> &indices, Expr *value,
            bool accumulate)
      : arg_(arg), indices_(), value_(value, this), accumulate_(accumulate) {
    assert(arg->getType()->getNumDims() == indices.size() &&
           "Invalid number of indices");
    for (auto *E : indices) {
      assert(E->getType().isIndexTy() && "Argument must be of index kind");
      indices_.emplace_back(E, this);
    }
  }

  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// This context is used when cloning programs or parts of programs. Values that
/// are stored in this map will be replaced when indexed by some expressions.
/// We use this context to replace things such as loop indices and arguments.
/// External values that are not in the map will not be replaced and the
/// original values will be used by the cloner.
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

  /// \returns the value that \p from is mapped to, or \p from if the value is
  /// not in the map.
  Loop *get(Loop *from) {
    auto it = loops.find(from);
    if (it != loops.end()) {
      return it->second;
    }
    return from;
  }
  /// \returns the value that \p from is mapped to, or \p from if the value is
  /// not in the map.
  Argument *get(Argument *from) {
    auto it = args.find(from);
    if (it != args.end()) {
      return it->second;
    }
    return from;
  }
};

} // namespace bistra

#endif // BISTRA_PROGRAM_PROGRAM_H
