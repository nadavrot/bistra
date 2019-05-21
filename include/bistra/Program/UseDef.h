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
    assert(ref_ == nullptr || ref_->getOwnerHandle() == this &&
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

} // namespace bistra

#endif // BISTRA_PROGRAM_USEDEF_H
