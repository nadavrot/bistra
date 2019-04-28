#ifndef BISTRA_PROGRAM_PROGRAM_H
#define BISTRA_PROGRAM_PROGRAM_H

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

/// This class represents a prorgam.
class Program final {
  /// \represents the list of arguments.
  std::vector<Argument> args_;

public:
  /// Argument getter.
  const std::vector<Argument> &getArgs() { return args_; }

  /// Adds a new argument;
  void addArgument(const std::string &name, std::vector<unsigned> dims,
                   ElemKind Ty);

  /// Adds a new argument;
  void addArgument(const Argument &arg);

  /// Prints the argument.
  void dump();
};

} // namespace bistra

#endif // BISTRA_PROGRAM_PROGRAM_H
