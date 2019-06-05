#include "bistra/Bytecode/Bytecode.h"
#include "bistra/Analysis/Visitors.h"
#include "bistra/Program/Program.h"

using namespace bistra;

StreamWriter::StreamWriter(std::string &str) : stream_(str) {}

void StreamWriter::write(uint32_t num) {
  write((uint8_t)(num >> 24));
  write((uint8_t)(num >> 16));
  write((uint8_t)(num >> 8));
  write((uint8_t)(num >> 0));
}

void StreamWriter::write(uint8_t num) { stream_.push_back(num); }

void StreamWriter::write(const std::string &s) {
  assert(s.size() < 256 && "String too long to serialize");
  write((uint8_t)s.size());
  for (auto &c : s) {
    stream_.push_back(c);
  }
}

StreamReader::StreamReader(const std::string &str) : stream_(str), pos_(0) {}

uint32_t StreamReader::readU32() {
  uint32_t res = 0;
  res = (res << 8) + readU8();
  res = (res << 8) + readU8();
  res = (res << 8) + readU8();
  res = (res << 8) + readU8();
  return res;
}

uint8_t StreamReader::readU8() {
  assert(stream_.size() > pos_);
  return stream_[pos_++];
}

std::string StreamReader::readStr() {
  auto len = readU8();
  std::string res;
  for (int i = 0; i < len; i++) {
    res.push_back(stream_[pos_++]);
  }

  return res;
}

bool StreamReader::hasMore() const { return pos_ != stream_.size(); }

void BytecodeHeader::serialize(StreamWriter &SW) {
  SW.write((uint32_t)0x03070102);

  // Serialize all of the names of the tensor dims.
  for (auto &tt : tensorTypeTable_.get()) {
    for (auto &name : tt.getNames()) {
      stringTable_.getIdFor(name);
    }
  }

  // Lock the table to make sure that we are not adding new strings during the
  /// serialization process. This is a debug feature.
  stringTable_.lock();

  // Write the number of strings:
  SW.write((uint32_t)stringTable_.get().size());
  // And write all of the strings.
  for (auto &str : stringTable_.get()) {
    SW.write(str);
  }

  // Write the number of exprTy:
  SW.write((uint32_t)exprTyTable_.get().size());
  // And write all of the expr types.
  for (auto &et : exprTyTable_.get()) {
    SW.write((uint8_t)et.getElementType());
    SW.write((uint8_t)et.getWidth());
  }

  // Write the number of tensor types:
  SW.write((uint32_t)tensorTypeTable_.get().size());
  // And write all of the tensor types.
  for (auto &tt : tensorTypeTable_.get()) {
    // Write the element kind.
    SW.write((uint8_t)tt.getElementType());

    // Write the number of dims.
    SW.write((uint8_t)tt.getNumDims());

    // Write the name and sizes of the dims.
    for (int i = 0; i < tt.getNumDims(); i++) {
      SW.write((uint32_t)tt.getDims()[i]);
      auto &dimName = tt.getNames()[i];
      SW.write((uint32_t)stringTable_.getIdFor(dimName));
    }
  }
}

void BytecodeHeader::deserialize(StreamReader &SR) {
  auto magic = SR.readU32();
  if (magic != 0x03070102) {
    assert("Invalid signature");
    return;
  }

  // Read the number of strings.
  auto n = SR.readU32();
  // And read the strings.
  for (int i = 0; i < n; i++) {
    auto ss = SR.readStr();
    stringTable_.getIdFor(ss);
  }

  // Read the number expr types.
  n = SR.readU32();
  // And read the types.
  for (int i = 0; i < n; i++) {
    auto tp = SR.readU8();
    auto width = SR.readU8();
    ExprType ET((ElemKind)tp, (unsigned)width);
    exprTyTable_.getIdFor(ET);
  }

  // Read the number tensor types.
  n = SR.readU32();

  // And read the tensor types.
  for (int i = 0; i < n; i++) {
    // Read the element type of the tensor.
    auto elemTy = SR.readU8();

    // Read the number of dims.
    auto numDims = SR.readU8();

    std::vector<unsigned> sizes;
    std::vector<std::string> names;

    for (int i = 0; i < numDims; i++) {
      sizes.push_back(SR.readU32());
      names.push_back(stringTable_.getById(SR.readU32()));
    }

    Type T((ElemKind)elemTy, sizes, names);
    tensorTypeTable_.getIdFor(T);
  }

  getStringTable().lock();
  getExprTyTable().lock();
  getTensorTypeTable().lock();
}

namespace {
/// A visitor class that collects all expressions in RPO.
struct ExprCollector : public NodeVisitor {
  std::vector<Expr *> &exprs_;
  ExprCollector(std::vector<Expr *> &exprs) : exprs_(exprs) {}
  virtual void leave(Expr *E) override {
    assert(std::find(exprs_.begin(), exprs_.end(), E) == exprs_.end() &&
           "Don't collect the same expr twice");
    exprs_.push_back(E);
  }

  /// Collect all of the expressions in the class.
  static std::vector<Expr *> getExprs(Stmt *s) {
    std::vector<Expr *> res;
    ExprCollector EC(res);
    s->visit(&EC);
    return res;
  }
};
} // namespace

enum SerializationTokenKind { ProgramKind };

std::string serialize(StreamWriter &SW, BytecodeHeader &BH, BytecodeContext &BC,
                      Expr *E) {}

std::string serialize(StreamWriter &SW, BytecodeHeader &BH, BytecodeContext &BC,
                      Stmt *S) {}

std::string Bytecode::serialize(Program *p) {
  std::string body;
  std::string header;
  BytecodeHeader BH;
  StreamWriter SR(body);

  auto exprs = ExprCollector::getExprs(p);

  // Function name.
  SR.write((uint32_t)BH.getStringTable().getIdFor(p->getName()));

  // How many arguments.
  SR.write((uint32_t)p->getArgs().size());
  // Each argument is described by name and type.
  for (auto &arg : p->getArgs()) {
    SR.write((uint32_t)BH.getStringTable().getIdFor(arg->getName()));
    SR.write((uint32_t)BH.getTensorTypeTable().getIdFor(*arg->getType()));
  }

  // How many local variables.
  SR.write((uint32_t)p->getVars().size());
  // Each argument is described by name and type.
  for (auto &var : p->getVars()) {
    SR.write((uint32_t)BH.getStringTable().getIdFor(var->getName()));
    SR.write((uint32_t)BH.getExprTyTable().getIdFor(var->getType()));
  }

  StreamWriter headerSR(header);
  BH.serialize(headerSR);
  return header + body;
}

Program *Bytecode::deserialize(const std::string &media) {
  BytecodeHeader BH;
  StreamReader SR(media);
  BH.deserialize(SR);

  std::string funcName = BH.getStringTable().getById(SR.readU32());

  Program *p = new Program(funcName, DebugLoc::npos());

  // Read the arguments:
  unsigned numArgs = SR.readU32();
  for (unsigned i = 0; i < numArgs; i++) {
    // Name + TensorType.
    auto name = BH.getStringTable().getById(SR.readU32());
    auto type = BH.getTensorTypeTable().getById(SR.readU32());
    p->addArgument(new Argument(name, type));
  }

  // Read the variables:
  unsigned numVars = SR.readU32();
  for (unsigned i = 0; i < numVars; i++) {
    // Name + TensorType.
    auto name = BH.getStringTable().getById(SR.readU32());
    auto type = BH.getExprTyTable().getById(SR.readU32());
    p->addVar(new LocalVar(name, type));
  }

  return p;
}