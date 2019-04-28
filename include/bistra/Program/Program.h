#ifndef BISTRA_PROGRAM_PROGRAM_H
#define BISTRA_PROGRAM_PROGRAM_H

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace bistra {

struct Type;
constexpr unsigned max_tensor_dimensions = 6;

/// An enum representing the type used by the elements of a tensor.
enum class ElemKind : unsigned char {
  Float32Ty, // 32-bit float type (float)
  Int8Ty,    // 8-bit type (int8_t)
  Int32Ty,   // 32-bit type (int32_t)
};

/// A class that represents a type of a tensor.
struct Type final {
  /// Contains the dimensions (sizes) of the tensor. Ex: [sx, sy, sz, ...].
  size_t sizes_[max_tensor_dimensions] = {
      0,
  };

  /// Contains the number of dimensions used by the tensor.
  unsigned char numSizes_{0};

  /// Specifies the element type of the tensor.
  ElemKind elementType_{ElemKind::Float32Ty};

  /// Initialize a new non-quantized type.
  Type(ElemKind elemTy, unsigned *dims, unsigned numDims)
      : numSizes_(numDims), elementType_(elemTy) {
    for (unsigned i = 0; i < numSizes_; i++) {
      sizes_[i] = dims[0];
    }
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
    if (numSizes_ != other.numSizes_) {
      return false;
    }
    // Sizes must be the same.
    for (size_t i = 0; i < numSizes_; i++) {
      if (sizes_[i] != other.sizes_[i]) {
        return false;
      }
    }

    return true;
  }

  ElemKind getElementType() const { return elementType_; }

  /// \returns the number of elements in the tensor.
  size_t size() const {
    size_t s = 1;
    for (unsigned char i = 0; i < numSizes_; i++) {
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
};

inline bool operator==(const Type &LHS, const Type &RHS) {
  return LHS.isEqual(RHS);
}

/// This class represents an input to the program, which is a Tensor, or a typed
/// region in memory.
class Buffer {};

/// This class represents a prorgam.
class Program {};

} // namespace bistra

#endif // BISTRA_PROGRAM_PROGRAM_H
