#ifndef BISTRA_PROGRAM_USEDEF_H
#define BISTRA_PROGRAM_USEDEF_H

#include <cassert>

namespace bistra {

template <class RefTy, class OwnerTy> class ASTHandle final {
  /// The reference that this handle manages.
  RefTy *ref_{nullptr};
  /// A reference to the ast node that owns this handle.
  OwnerTy *parent_;

public:
  ASTHandle(RefTy *ref, OwnerTy *owner) : parent_(owner) { setReference(ref); }
  ~ASTHandle() { delete ref_; }

  /// \returns the ASTNode that holds this handle.
  OwnerTy *getParent() const { return parent_; }

  void setReference(RefTy *ref) {
    // Unregister the previous reference.
    if (ref_) {
      ref_->resetUse();
    }

    // Register the new reference.
    ref_ = ref;
    if (ref_) {
      // Reset the old handle.
      if (auto *EH = ref_->getUse()) {
        EH->ref_ = nullptr;
      }
      // Register this as the new handle.
      ref_->resetUse(this);
    }
    verify();
  }

  RefTy *get() const {
    verify();
    return ref_;
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
    assert(ref_ == nullptr ||
           ref_->getUse() == this && "The handle pointes to an unowned ref.");
  }

  operator RefTy *() { return ref_; }

  ASTHandle(const ASTHandle &other) = delete;
  ASTHandle(const ASTHandle &&other) {
    parent_ = other.parent_;
    setReference(other.ref_);
  }
  ASTHandle &operator=(ASTHandle &other) = delete;
  ASTHandle &operator=(ASTHandle &&other) = delete;
};

} // namespace bistra

#endif // BISTRA_PROGRAM_USEDEF_H
