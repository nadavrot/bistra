#ifndef BISTRA_PROGRAM_TYPES_H
#define BISTRA_PROGRAM_TYPES_H

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

  /// \returns the number of scalars in the tensor.
  unsigned getSize() {
    unsigned size = 1;
    // Multiply all of the dimensions.
    for (auto d : sizes_) {
      size *= d;
    }
    return size;
  }

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
        "int8_t",
        "size_t",
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

} // namespace bistra

#endif // BISTRA_PROGRAM_TYPES_H
