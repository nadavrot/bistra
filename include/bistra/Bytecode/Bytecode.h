#ifndef BISTRA_BYTECODE_BYTECODE_H
#define BISTRA_BYTECODE_BYTECODE_H

#include "bistra/Program/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bistra {

class Program;

/// A class for handling a list of resources that are indexed by ID.
template <typename ElemTy> class IdTable {
  /// Stores the elements.
  std::vector<ElemTy> table_;
  /// If True then the table must not grow.
  bool locked_{false};

public:
  /// \returns the backing table.
  std::vector<ElemTy> &get() { return table_; }

  /// \returns the size of the table.
  unsigned size() { return table_.size(); }

  /// Lock the table and assert if we try to grow it. This is a debug feature.
  void lock() { locked_ = true; }

  /// \returns the ID that saves \p T.
  /// The table may add a new entry to contain \p T.
  unsigned getIdFor(const ElemTy &T) {
    for (unsigned i = 0; i < table_.size(); i++) {
      if (table_[i] == T)
        return i;
    }
    assert(!locked_ && "Table must be unlocked");
    table_.push_back(T);
    return table_.size() - 1;
  }

  /// \return some element that is stored at id \p Id.
  const ElemTy &getById(unsigned Id) const {
    assert(table_.size() > Id && "Element not in table");
    return table_[Id];
  }
};

/// Wraps an output stream.
class StreamWriter {
  /// The backing stream.
  std::string &stream_;

public:
  /// Write to the backing string \p str.
  StreamWriter(std::string &str);
  /// Write a word.
  void write(uint32_t num);

  /// Write byte.
  void write(uint8_t num);

  /// Write a null terminated string.
  void write(const std::string &s);
};

/// Wraps an input stream.
class StreamReader {
  /// The backing stream.
  std::string &stream_;
  /// The position in the stream.
  size_t pos_;

public:
  /// Read from the backing string \p str.
  StreamReader(std::string &str);

  /// read a word.
  uint32_t readU32();

  /// read a byte.
  uint8_t readU8();

  /// Read a null-terminated string.
  std::string readStr();

  /// \return true if the stream has more data to read.
  bool hasMore();
};

/// Sarializes and deserializes bytecode header.
class BytecodeHeader {
  /// Maps strings to integers.
  IdTable<std::string> stringTable_;
  /// Maps ExprType to integers.
  IdTable<ExprType> exprTyTable_;
  /// Maps Types to integers.
  IdTable<Type> tensorTypeTable_;

public:
  IdTable<std::string> &getStringTable() { return stringTable_; }
  IdTable<ExprType> &getExprTyTable() { return exprTyTable_; }
  IdTable<Type> &getTensorTypeTable() { return tensorTypeTable_; }

  /// Write the header to \p SR.
  void serialize(StreamWriter &SW);

  /// Read the header from \p SR.
  void deserialize(StreamReader &SR);
};

/// Sarializes and deserializes bytecode.
class Bytecode {
  BytecodeHeader header;

public:
  Bytecode() = default;

  void read(const std::string &path);

  void save(const std::string &path);

  void serialize(Program *p);

  Program *deserialize();
};

} // namespace bistra

#endif // BISTRA_BYTECODE_BYTECODE_H
