#ifndef BISTRA_PARSER_PARSERCONTEXT_H
#define BISTRA_PARSER_PARSERCONTEXT_H

#include "bistra/Base/Base.h"
#include "bistra/Program/Pragma.h"

#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>

namespace bistra {
class Program;
class Expr;
class Argument;
class Loop;
class LocalVar;

/// A segmented scoped stack that contains groups of named values.
template <typename ElemTy> class ScopedNamedValueStack {
  std::vector<std::pair<std::string, ElemTy *>> stack_;

public:
  /// \returns the size of the stack that keeps the expression. This
  /// allows us to unwind and get rid of unused lets when the 'var' go out of
  /// scope.
  unsigned getStackLevel() const { return stack_.size(); }

  /// Restore the stack to the level of \p handle.
  void restoreStack(unsigned handle) {
    assert(stack_.size() >= handle && "Invalid state");
    while (stack_.size() > handle) {
      delete stack_.back().second;
      stack_.pop_back();
    }
  }

  /// \returns the stored expression or nullptr if the name was not found.
  ElemTy *getByName(const std::string &name) const {
    // Go over the 'let' expression in reverse to find the last decleration of
    // the variable.
    for (int i = stack_.size() - 1; i >= 0; i--) {
      auto let = stack_[i];
      if (let.first == name)
        return let.second;
    }
    return nullptr;
  }

  /// Register the expression \p e under the name \p name.
  void registerValue(const std::string &name, ElemTy *e) {
    stack_.push_back(std::make_pair(name, e));
  }
};

/// Maps global values to names.
template <typename ElemTy> class NamedValueMap {
  /// Indexes values by name.
  using storageTy = std::vector<ElemTy *>;
  storageTy map_;

public:
  using iterator = typename storageTy::iterator;

  iterator begin() { return map_.begin(); }
  iterator end() { return map_.end(); }

  /// Registers a new value.
  void registerValue(ElemTy *arg) {
    assert(getByName(arg->getName()) == nullptr &&
           "Argument already registered");
    map_.push_back(arg);
  }

  /// \returns the argument with the name \p name or nullptr.
  ElemTy *getByName(const std::string &name) const {
    for (auto *a : map_) {
      if (a->getName() == name)
        return a;
    }
    return nullptr;
  }
};

/// The context that serves the parser while parsing.
class ParserContext {
  /// The name of the file that we parse.
  std::string filename_;
  /// The base pointer for the parsed buffer.
  const char *buffer_;

  /// The parsed program.
  Program *prog_{nullptr};

  /// Counts the number of errors that were emitted.
  unsigned numErrors_{0};
  /// Counts the number of warnings that were emitted.
  unsigned numWarnings_{0};
  /// Counts the number of notes that were emitted.
  unsigned numNotes_{0};

  /// A list of pragma declerations.
  std::vector<PragmaCommand> pragmas_;

  /// Indexes arguments by name.
  NamedValueMap<Argument> argMap_;

  /// Indexes variables by name.
  NamedValueMap<LocalVar> varMap_;

  /// Contains the next of loops while parsing.
  std::vector<Loop *> loopNextStack_;

  /// Contains the stack of 'let' expressions.
  ScopedNamedValueStack<Expr> letStack_;

  /// Contains the stack of local variables.
  ScopedNamedValueStack<LocalVar> varStack_;

public:
  ParserContext(const char *buffer, const std::string &filename = "")
      : filename_(filename), buffer_(buffer) {}

  const char *getBuffer() const { return buffer_; }

  /// \returns a loop by name \p name that is currently in the loop nest stack.
  Loop *getLoopByName(const std::string &name);

  /// Add the loop \p L to the loop stack.
  void pushLoop(Loop *L);

  /// Remove the top loop from the loop stack.
  Loop *popLoop();

  /// \returns the Let stack.
  ScopedNamedValueStack<Expr> &getLetStack() { return letStack_; }

  /// \returns the Var stack.
  ScopedNamedValueStack<LocalVar> &getVarStack() { return varStack_; }

  /// \returns the Var map.
  NamedValueMap<LocalVar> &getVarMap() { return varMap_; }

  /// \returns the Var stack.
  NamedValueMap<Argument> &getArgMap() { return argMap_; }

  /// Saves the parsed program when done.
  void registerProgram(Program *p);

  /// \returns the parsed program or nullptr.
  Program *getProgram() { return prog_; }

  /// \returns the number of errors.
  unsigned getNumErrors() { return numErrors_; }

  /// \returns the pragma declerations that were applied to loops.
  std::vector<PragmaCommand> &getPragmaDecls() { return pragmas_; }

  /// Adds the pragma to the list of applied pragmas.
  void addPragma(PragmaCommand &pc);

  /// \returns the line and column for the position \p pos.
  std::pair<unsigned, unsigned> getLineCol(DebugLoc pos);

  enum DiagnoseKind { Error, Warning, Note };
  /// Emit an error message.
  void diagnose(DiagnoseKind kind, DebugLoc loc, const std::string &message);
};

} // namespace bistra

#endif // BISTRA_PARSER_PARSERCONTEXT_H
