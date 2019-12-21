#ifndef BISTRA_PROGRAM_PROGRAM_H
#define BISTRA_PROGRAM_PROGRAM_H

#include "bistra/Base/Base.h"
#include "bistra/Program/Types.h"
#include "bistra/Program/UseDef.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bistra {

class CloneCtx;

class Expr;
class Stmt;
class NodeVisitor;

/// This class represents an input to the program, which is a Tensor, or a
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

  /// Sets the type of the argument.
  void setType(Type &t) { type_ = t; }

  /// \returns the name of the argument.
  const std::string &getName() const { return name_; }

  /// Prints the argument.
  void dump() const;

  /// \returns a hash for this Argument.
  uint64_t hash() const;

  /// Crash if the program is in an invalid state.
  void verify() const;
};

/// This class represents a local variable in the program.
class LocalVar final {
  /// The name of the variable.
  std::string name_;

  /// The type of the variable.
  ExprType type_;

public:
  LocalVar(const std::string &name, const ExprType &t)
      : name_(name), type_(t) {}

  /// \returns the type of the variable.
  const ExprType getType() const { return type_; }

  /// \returns the name of the variable.
  const std::string &getName() const { return name_; }

  /// Prints the variable.
  void dump() const;

  /// \returns a hash for this Argument.
  uint64_t hash() const;

  /// Crash if the program is in an invalid state.
  void verify() const;
};

class Stmt : public ASTNode {
  /// A nullable pointer to the handle that may contain this statement.
  bistra::StmtHandle *user_{nullptr};

public:
  Stmt() = delete;
  Stmt(const Stmt &) = delete;
  Stmt(DebugLoc loc) : ASTNode(loc) {}

  /// Prints the statement.
  virtual void dump(unsigned indent) const = 0;

  /// \returns True if this is identical to the other statement.
  virtual bool compare(const Stmt *other) const = 0;

  /// \returns a hash for this statement.
  virtual uint64_t hash() const = 0;

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
  Expr(const ExprType &ty, DebugLoc loc) : ASTNode(loc), type_(ty) {}

  Expr(ElemKind &kind, DebugLoc loc) : ASTNode(loc), type_(ExprType(kind)) {}

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

  /// \returns True if this is identical to the other expression.
  virtual bool compare(const Expr *other) const = 0;

  /// \returns a hash for this expression.
  virtual uint64_t hash() const = 0;

  Expr() = delete;
  Expr(const Expr &other) = delete;
};

/// Represents a list of statements that are executed sequentially.
class Scope : public Stmt {
protected:
  /// Holds the body of the loop.
  std::vector<StmtHandle> body_;

public:
  Scope(const std::vector<Stmt *> &body, DebugLoc loc) : Stmt(loc), body_() {
    for (auto *stmt : body) {
      body_.emplace_back(stmt, this);
    }
  }

  Scope(DebugLoc loc) : Stmt(loc) {}

  /// \returns True if the body of the loop is empty.
  bool isEmpty() { return !body_.size(); }

  /// Empties the body of the scope.
  void clear();

  /// Moves the statements from \p other to this scope.
  void takeContent(Scope *other);

  /// Add a statement to the end of the scope.
  void addStmt(Stmt *s);

  /// \remove the statement \p s, if it is in the body.
  void removeStmt(Stmt *s);

  /// Replace and delete the old statement \p oldS with \p newS.
  /// \p oldS must be in the scope body.
  void replaceStmt(Stmt *newS, Stmt *oldS);

  /// Insert the statement \p s before \p where. \p where must be in the body.
  void insertBeforeStmt(Stmt *s, Stmt *where);

  /// Insert the statement \p s after \p where. \p where must be in the body.
  void insertAfterStmt(Stmt *s, Stmt *where);

  /// \returns the body of the loop.
  std::vector<StmtHandle> &getBody() { return body_; }

  /// \returns the body of the loop.
  const std::vector<StmtHandle> &getBody() const { return body_; }

  virtual bool compare(const Stmt *other) const override;
  virtual uint64_t hash() const override;
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
  Loop(std::string name, DebugLoc loc, unsigned end, unsigned stride = 1)
      : Scope(loc), indexName_(name), end_(end), stride_(stride) {}

  /// \returns the name of the induction variable.
  const std::string &getName() const { return indexName_; }

  /// \returns the name of the induction variable.
  void setName(const std::string &name) { indexName_ = name; }

  /// \returns the end point of the loop.
  unsigned getEnd() const { return end_; }

  /// Sets the range end point;
  void setEnd(unsigned tc) { end_ = tc; }

  /// \returns the loop stride factor.
  unsigned getStride() const { return stride_; }

  /// Updated the loop stride.
  void setStride(unsigned s) { stride_ = s; }

  virtual bool compare(const Stmt *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Represents an if-in-range construct with the half-open range.
/// The range must be a valid non-zero length range (start <= end).
/// Example:  if (x in 0 .. 15).
class IfRange final : public Scope {
  /// The numeric value to evaluate.
  ExprHandle val_;

  /// The start of the range.
  int start_;

  /// The end of the range.
  int end_;

public:
  IfRange(Expr *val, int start, int end, DebugLoc loc)
      : Scope(loc), val_(val, this), start_(start), end_(end) {}

  /// Sets the if-range range.
  void setRange(std::pair<int, int> range) {
    start_ = range.first;
    end_ = range.second;
  }

  /// \returns the index expr.
  ExprHandle &getIndex() { return val_; }

  /// \returns the index expr.
  const ExprHandle &getIndex() const { return val_; }

  /// \returns the if-range range.
  std::pair<int, int> getRange() const { return {start_, end_}; }

  virtual bool compare(const Stmt *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// This class represents a program.
class Program final : public Scope {
  /// The name of the program.
  std::string name_;
  /// \represents the list of arguments.
  std::vector<Argument *> args_;
  /// \represents the list of local variables.
  std::vector<LocalVar *> vars_;

public:
  ~Program();

  Program(const std::string &name, DebugLoc loc);

  /// \returns the name of the program.
  const std::string &getName() const { return name_; }

  /// Construct a new program with the body \p body and arguments \p args.
  Program(const std::string &name, const std::vector<Stmt *> &body,
          const std::vector<Argument *> &args,
          const std::vector<LocalVar *> &vars, DebugLoc loc);

  /// Argument getter.
  std::vector<Argument *> &getArgs() { return args_; }
  /// Vars getter.
  std::vector<LocalVar *> &getVars() { return vars_; }

  /// Argument getter.
  const std::vector<Argument *> &getArgs() const { return args_; }
  /// Vars getter.
  const std::vector<LocalVar *> &getVars() const { return vars_; }

  /// \return the variable with the name \p name or nullptr if there is no
  /// variable with this name.
  LocalVar *getVar(const std::string &name);

  /// \returns the n'th argument.
  Argument *getArg(unsigned idx) {
    assert(idx < args_.size() && "Invalid arg index");
    return args_[idx];
  }

  /// \returns the argument index number for \p arg.
  unsigned getArgIndex(Argument *arg) {
    unsigned idx = 0;
    for (auto *a : args_) {
      if (a == arg)
        return idx;
      idx++;
    }
    assert(false && "invalid argument");
    return 0;
  }

  /// \returns the n'th argument.
  LocalVar *getVar(unsigned idx) {
    assert(idx < vars_.size() && "Invalid var index");
    return vars_[idx];
  }

  /// \returns the var index number for \p var.
  unsigned getVarIndex(LocalVar *var) {
    unsigned idx = 0;
    for (auto *a : vars_) {
      if (a == var)
        return idx;
      idx++;
    }
    assert(false && "invalid variable");
    return 0;
  }

  /// Adds a new argument. \returns the newly created argument.
  Argument *addArgument(const std::string &name,
                        const std::vector<unsigned> &dims,
                        const std::vector<std::string> &names, ElemKind Ty);

  /// Create a new variable. \returns the newly created variable.
  LocalVar *addLocalVar(const std::string &name, ExprType Ty);

  /// Create a new temporary variable with the type \p Ty with a unique name
  /// that is similar to \p nameHint .
  /// \returns the newly created variable.
  LocalVar *addTempVar(const std::string &nameHint, ExprType Ty);

  /// Adds a new argument;
  void addArgument(Argument *arg);

  /// Adds a new argument;
  void addVar(LocalVar *arg);

  Program *clone();

  virtual bool compare(const Stmt *other) const override;
  virtual uint64_t hash() const override;
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
  IndexExpr(Loop *loop)
      : Expr(ElemKind::IndexTy, loop->getLoc()), loop_(loop) {}

  IndexExpr(Loop *loop, DebugLoc loc)
      : Expr(ElemKind::IndexTy, loc), loop_(loop) {}

  IndexExpr(Loop *loop, const ExprType &ty)
      : Expr(ty, loop->getLoc()), loop_(loop) {}

  /// \returns the loop that this expression indexes.
  Loop *getLoop() const { return loop_; }

  /// Update the loop index. We use this API during model serialization.
  void setLoop(Loop *L) { loop_ = L; }

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
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
  ConstantExpr(int64_t val)
      : Expr(ElemKind::IndexTy, DebugLoc::npos()), val_(val) {}

  /// \returns the value stored by this constant.
  int64_t getValue() const { return val_; }

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// A constant float expression.
class ConstantFPExpr final : public Expr {
  /// The value that this constant float represents.
  float val_;

public:
  ConstantFPExpr(float val)
      : Expr(ElemKind::Float32Ty, DebugLoc::npos()), val_(val) {}

  /// \returns the value stored by this constant.
  float getValue() const { return val_; }

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// A constant string expression.
class ConstantStringExpr final : public Expr {
  /// The value that this constant string represents.
  std::string val_;

public:
  ConstantStringExpr(const std::string &val)
      : Expr(ElemKind::IndexTy, DebugLoc::npos()), val_(val) {}

  /// \returns the value stored by this constant.
  const std::string &getValue() const { return val_; }

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// A binary arithmetic expression.
class BinaryExpr : public Expr {
public:
  enum BinOpKind { Mul, Add, Div, Sub, Max, Min, Pow };

protected:
  /// Left-hand-side of the expression.
  ExprHandle LHS_;
  /// Right-hand-side of the expression.
  ExprHandle RHS_;
  /// The kind of the binary operator.
  BinOpKind kind_;

public:
  BinaryExpr(Expr *LHS, Expr *RHS, BinOpKind kind, DebugLoc loc)
      : Expr(LHS->getType(), loc), LHS_(LHS, this), RHS_(RHS, this),
        kind_(kind) {
    assert(LHS->getType() == RHS->getType() && "Invalid expr type");
    assert(LHS != RHS && "Invalid ownership of operands");
  }

  ~BinaryExpr() = default;

  /// \return the kind of the binary operator.
  BinOpKind getKind() const { return kind_; }

  /// \returns True if the operation is commutative.
  bool isCommutative() const;

  /// \returns the string representation of \p kind_;
  static const char *getOpSymbol(BinOpKind kind_);

  /// \returns the string representation of this expression;
  const char *getOpSymbol() const;

  Expr *getLHS() const { return LHS_.get(); }
  Expr *getRHS() const { return RHS_.get(); }

  void setLHS(Expr *e) { return LHS_.setReference(e); }
  void setRHS(Expr *e) { return RHS_.setReference(e); }

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Unary arithmetic expression.
class UnaryExpr : public Expr {
public:
  enum UnaryOpKind { Exp, Log, Sqrt, Abs };

protected:
  /// The child expression.
  ExprHandle val_;

  /// The kind of the operator.
  UnaryOpKind kind_;

public:
  UnaryExpr(Expr *val, UnaryOpKind kind, DebugLoc loc)
      : Expr(val->getType(), loc), val_(val, this), kind_(kind) {}

  ~UnaryExpr() = default;

  /// \return the kind of the operator.
  UnaryOpKind getKind() const { return kind_; }

  /// \returns the string representation of \p kind_;
  static const char *getOpSymbol(UnaryOpKind kind_);

  /// \returns the string representation of this expression;
  const char *getOpSymbol() const;

  Expr *getVal() const { return val_.get(); }
  void setVal(Expr *e) { return val_.setReference(e); }

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Broadcasts a value from scalar to vector.
class BroadcastExpr : public Expr {
  /// The value to broadcast.
  ExprHandle val_;
  /// The vectorization width.
  unsigned vf_;

public:
  BroadcastExpr(Expr *val, unsigned vf)
      : Expr(val->getType().asVector(vf), val->getLoc()), val_(val, this),
        vf_(vf) {}

  /// \returns the broadcasted value.
  Expr *getValue() const { return val_.get(); }

  /// \returns the vectorization factor.
  unsigned getVF() const { return vf_; }

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Get the pointer to the array expression.
class GEPExpr final : public Expr {
  /// The buffer to access.
  Argument *arg_;
  /// The indices for indexing the buffer.
  std::vector<ExprHandle> indices_;

public:
  /// \returns the buffer destination of the instruction.
  Argument *getDest() const { return arg_; }

  /// \returns the indices indexing into the array.
  const std::vector<ExprHandle> &getIndices() const { return indices_; }

  /// \returns the indices indexing into the array.
  std::vector<ExprHandle> &getIndices() { return indices_; }

  GEPExpr(Argument *arg, const std::vector<Expr *> &indices, DebugLoc loc)
      : Expr(ElemKind::PtrTy, loc), arg_(arg) {
    for (auto *E : indices) {
      assert(E->getType().isIndexTy() && "Argument must be of index kind");
      indices_.emplace_back(E, this);
    }
    assert(arg->getType()->getNumDims() == indices_.size() &&
           "Invalid number of indices");
  }

  ~GEPExpr() = default;

  /// \returns True if the other GEP \p another points to the same location.
  bool isSameAddres(GEPExpr *another);

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Loads some value from a buffer.
class LoadExpr final : public Expr {
  /// The buffer to load.
  ExprHandle gep_;

public:
  /// \returns the GEP expression.
  GEPExpr *getGep() const { return (GEPExpr *)gep_.get(); }

  /// \returns the buffer destination of the instruction.
  Argument *getDest() const { return gep_.as<GEPExpr>()->getDest(); }

  /// \returns the indices indexing into the array.
  const std::vector<ExprHandle> &getIndices() const {
    return gep_.as<GEPExpr>()->getIndices();
  }

  /// \returns the indices indexing into the array.
  std::vector<ExprHandle> &getIndices() {
    return gep_.as<GEPExpr>()->getIndices();
  }

  LoadExpr(GEPExpr *gep, DebugLoc loc);

  LoadExpr(GEPExpr *gep, ExprType elemTy, DebugLoc loc);

  /// Convenient ctor that initializes a GEP experssion.
  LoadExpr(Argument *arg, const std::vector<Expr *> &indices, ExprType elemTy,
           DebugLoc loc);

  /// Convenient ctor that initializes a GEP experssion.
  LoadExpr(Argument *arg, const std::vector<Expr *> &indices, DebugLoc loc);

  ~LoadExpr() = default;

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Loads some value from a local variable.
class LoadLocalExpr final : public Expr {
  /// The variable to access.
  LocalVar *var_{nullptr};

public:
  /// \returns the accessed variable.
  LocalVar *getDest() const { return var_; }

  LoadLocalExpr(LocalVar *var, DebugLoc loc)
      : Expr(var->getType(), loc), var_(var) {}

  virtual bool compare(const Expr *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump() const override;
  virtual Expr *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Calls some external function with parameters.
class CallStmt final : public Stmt {
  /// The function name to call.
  std::string name_;
  /// The parameters to pass.
  std::vector<ExprHandle> params_;

public:
  /// \returns the name of the callee.
  std::string getName() const { return name_; }

  /// \returns the call parameters.
  const std::vector<ExprHandle> &getParams() const { return params_; }

  /// \returns the call parameters.
  std::vector<ExprHandle> &getParams() { return params_; }

  CallStmt(const std::string &name, const std::vector<Expr *> &params,
           DebugLoc loc)
      : Stmt(loc), name_(name), params_() {
    for (auto *E : params) {
      params_.emplace_back(E, this);
    }
  }

  /// Clone indices and return the list of unowned expr indices.
  std::vector<Expr *> cloneIndicesPtr(CloneCtx &map);

  virtual bool compare(const Stmt *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Stores some value to a buffer.
class StoreStmt final : public Stmt {
  /// The buffer to access.
  ExprHandle gep_;
  /// The value to store into the buffer.
  ExprHandle value_;
  /// Accumulate the resule into the destination.
  bool accumulate_;

public:
  /// \returns the GEP expression.
  GEPExpr *getGep() const { return gep_.as<GEPExpr>(); }

  /// \returns the buffer destination of the instruction.
  Argument *getDest() const { return gep_.as<GEPExpr>()->getDest(); }

  /// \returns the indices indexing into the array.
  const std::vector<ExprHandle> &getIndices() const {
    return gep_.as<GEPExpr>()->getIndices();
  }

  /// \returns the indices indexing into the array.
  std::vector<ExprHandle> &getIndices() {
    return gep_.as<GEPExpr>()->getIndices();
  }

  /// \returns the expression of the last index.
  const ExprHandle &getLastIndex() const {
    return getIndices()[getIndices().size() - 1];
  }

  /// \returns the expression of the last index.
  ExprHandle &getLastIndex() { return getIndices()[getIndices().size() - 1]; }
  /// \returns the stored value.
  ExprHandle &getValue() { return value_; }

  /// \returns true if this store statement accumulates into the stored
  /// destination.
  bool isAccumulate() { return accumulate_; }

  StoreStmt(GEPExpr *gep, Expr *value, bool accumulate, DebugLoc loc);

  StoreStmt(Argument *arg, const std::vector<Expr *> &indices, Expr *value,
            bool accumulate, DebugLoc loc);

  /// Clone indices and return the list of unowned expr indices.
  std::vector<Expr *> cloneIndicesPtr(CloneCtx &map);

  virtual bool compare(const Stmt *other) const override;
  virtual uint64_t hash() const override;
  virtual void dump(unsigned indent) const override;
  virtual Stmt *clone(CloneCtx &map) override;
  virtual void verify() const override;
  virtual void visit(NodeVisitor *visitor) override;
};

/// Saves some value to a local.
class StoreLocalStmt final : public Stmt {
  /// The variable to access.
  LocalVar *var_;
  /// The value to store into the buffer.
  ExprHandle value_;
  /// Accumulate the resule into the destination.
  bool accumulate_;

public:
  /// \returns the write destination of the instruction.
  LocalVar *getDest() { return var_; }

  /// \returns the stored value.
  ExprHandle &getValue() { return value_; }

  /// \returns true if this store statement accumulates into the stored
  /// destination.
  bool isAccumulate() { return accumulate_; }

  StoreLocalStmt(LocalVar *var, Expr *value, bool accumulate, DebugLoc loc)
      : Stmt(loc), var_(var), value_(value, this), accumulate_(accumulate) {
    assert(value->getType() == var->getType() && "invalid stored type");
  }

  virtual bool compare(const Stmt *other) const override;
  virtual uint64_t hash() const override;
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
  // Maps local vars.
  std::unordered_map<LocalVar *, LocalVar *> vars;
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
  /// Maps \p From to \p To and returns \p To.
  LocalVar *map(LocalVar *from, LocalVar *to) {
    assert(vars.count(from) == 0 && "LocalVar already in map");
    vars[from] = to;
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
  /// \returns the value that \p from is mapped to, or \p from if the value is
  /// not in the map.
  LocalVar *get(LocalVar *from) {
    auto it = vars.find(from);
    if (it != vars.end()) {
      return it->second;
    }
    return from;
  }
};

} // namespace bistra

#endif // BISTRA_PROGRAM_PROGRAM_H
