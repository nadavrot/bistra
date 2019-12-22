#ifndef BISTRA_PROGRAM_USEDEF_H
#define BISTRA_PROGRAM_USEDEF_H

#include "bistra/Base/Base.h"

#include <cassert>

namespace bistra {

template <class RefTy, class OwnerTy> class ASTHandle final {
  /// The reference that this handle manages.
  RefTy *ref_{nullptr};
  /// A reference to the ast node that owns this handle.
  OwnerTy *parent_;

public:
  ASTHandle(RefTy *ref, OwnerTy *owner) : parent_(owner) { setReference(ref); }
  ~ASTHandle() {
    if (ref_) {
      ref_->resetOwnerHandle();
      delete ref_;
    }
  }

  /// \returns the ASTNode that holds this handle.
  OwnerTy *getParent() const { return parent_; }

  void setReference(RefTy *ref) {
    // Unregister the previous reference.
    if (ref_) {
      ref_->resetOwnerHandle();
    }

    // Register the new reference.
    ref_ = ref;
    if (ref_) {
      // Reset the old handle.
      if (auto *EH = ref_->getOwnerHandle()) {
        EH->ref_ = nullptr;
      }
      // Register this as the new handle.
      ref_->resetOwnerHandle(this);
    }
    verify();
  }

  template <typename Ty>
  /// \returns a copy of the owned ptr casted to type Ty.
  Ty *as() const {
    verify();
    return (Ty *)(ref_);
  }

  /// \returns a copy of the owned ptr.
  RefTy *get() const {
    verify();
    return ref_;
  }

  /// Take the pointer away from this handle and set it to nullptr.
  RefTy *take() {
    verify();
    auto *old = ref_;
    setReference(nullptr);
    return old;
  }

  RefTy *operator->() {
    verify();
    return ref_;
  }

  const RefTy *operator->() const {
    verify();
    return ref_;
  }

  void verify() const {
    assert((ref_ == nullptr || ref_->getOwnerHandle() == this) &&
           "The handle pointes to an unowned ref.");
  }

  operator RefTy *() { return ref_; }

  ASTHandle(const ASTHandle &other) = delete;
  ASTHandle(const ASTHandle &&other) {
    parent_ = other.parent_;
    setReference(other.ref_);
  }
  ASTHandle &operator=(ASTHandle &other) = delete;
  ASTHandle &operator=(ASTHandle &&other) {
    parent_ = other.parent_;
    setReference(other.ref_);
    return *this;
  }
};

class Program;
class NodeVisitor;
class Expr;
class Stmt;

class ASTNode {
  DebugLoc loc_;

public:
  ASTNode() = delete;
  ASTNode(const ASTNode &) = delete;

  /// \returns the debug location for the node.
  DebugLoc getLoc() const { return loc_; }

  ASTNode(DebugLoc loc) : loc_(loc) {}
  /// \returns the parent expression that holds the node of this expression.
  virtual ASTNode *getParent() const = 0;
  /// Crash if the program is in an invalid state.
  virtual void verify() const = 0;
  /// A node visitor that visits all of the nodes in the program.
  virtual void visit(NodeVisitor *visitor) = 0;
  /// Walk up the chain and find the owning program. The node must be owned.
  Program *getProgram() const;
};

using ExprHandle = ASTHandle<Expr, ASTNode>;
using StmtHandle = ASTHandle<Stmt, ASTNode>;

} // namespace bistra

#endif // BISTRA_PROGRAM_USEDEF_H
