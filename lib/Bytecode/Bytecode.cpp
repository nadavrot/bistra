#include "bistra/Bytecode/Bytecode.h"
#include "bistra/Analysis/Value.h"
#include "bistra/Program/Program.h"

#include <unordered_map>

using namespace bistra;

StreamWriter::StreamWriter(std::string &str) : stream_(str) {}

void StreamWriter::write(uint32_t num) {
  write((uint8_t)(num >> 24));
  write((uint8_t)(num >> 16));
  write((uint8_t)(num >> 8));
  write((uint8_t)(num >> 0));
}

void StreamWriter::write(float num) {
  float f = num;
  uint32_t val;
  memcpy(&val, &f, sizeof(float));
  write((uint32_t)val);
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

float StreamReader::readF32() {
  uint32_t val = readU32();
  float f;
  memcpy(&f, &val, sizeof(float));
  return f;
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

enum ExprTokenKind {
  ConstantExprKind,
  ConstantFPExprKind,
  ConstantStringExprKind,
  BinaryExprKind,
  UnaryExprKind,
  LoadExprKind,
  LoadLocalExprKind,
  BroadcastExprKind,
  IndexExprKind,
  LastExprKind
};

enum StmtTokenKind {
  LoopKind,
  CallStmtKind,
  StoreStmtKind,
  StoreLocalStmtKind,
  IfRangeKind,
  LastStmtKind
};

/// Sarialization Context.
struct bistra::SerializeContext {
  IdTable<Expr *> exprTable_;
  IdTable<Stmt *> stmtTable_;
};

/// Deserialization Context.
struct bistra::DeserializeContext {
  std::unordered_map<unsigned, Stmt *> stmtMap_;
  std::unordered_map<unsigned, Expr *> exprMap_;
  /// A list of indices to resolve after parsing is complete.
  std::unordered_map<IndexExpr *, unsigned> resolveLater_;

  void registerExpr(unsigned id, Expr *e) {
    assert(!exprMap_.count(id) && "id already in map");
    exprMap_[id] = e;
  }
  void registerResolveLater(IndexExpr *LI, unsigned stmtId) {
    assert(!resolveLater_.count(LI) && "LI already in map");
    resolveLater_[LI] = stmtId;
  }

  void registerStmt(unsigned id, Stmt *s) {
    assert(!stmtMap_.count(id) && "id already in map");
    stmtMap_[id] = s;
  }
  Expr *getExpr(unsigned id) {
    assert(exprMap_.count(id) && "id not in map");
    return exprMap_[id];
  }
  Stmt *getStmt(unsigned id) {
    assert(stmtMap_.count(id) && "id not in map");
    return stmtMap_[id];
  }
};

void Bytecode::serialize(StreamWriter &SW, BytecodeHeader &BH,
                         SerializeContext &BC, Program *p, Expr *E) {

  if (auto *CE = dynamic_cast<ConstantExpr *>(E)) {
    // Kind:
    SW.write((uint32_t)ExprTokenKind::ConstantExprKind);
    // My ID:
    SW.write((uint32_t)BC.exprTable_.getIdFor(CE));
    // Value:
    SW.write((uint32_t)CE->getValue());
    return;
  }

  if (auto *CE = dynamic_cast<ConstantFPExpr *>(E)) {
    // Kind:
    SW.write((uint32_t)ExprTokenKind::ConstantFPExprKind);
    // My ID:
    SW.write((uint32_t)BC.exprTable_.getIdFor(CE));
    // Value:
    SW.write((float)CE->getValue());
    return;
  }

  if (auto *CSE = dynamic_cast<ConstantStringExpr *>(E)) {
    // Kind:
    SW.write((uint32_t)ExprTokenKind::ConstantStringExprKind);
    // My ID:
    SW.write((uint32_t)BC.exprTable_.getIdFor(CSE));
    // Value:
    SW.write((uint32_t)BH.getStringTable().getIdFor(CSE->getValue()));
    return;
  }

  if (auto *BE = dynamic_cast<BinaryExpr *>(E)) {
    // Kind:
    SW.write((uint32_t)ExprTokenKind::BinaryExprKind);
    // My ID:
    SW.write((uint32_t)BC.exprTable_.getIdFor(BE));
    // OpKind:
    SW.write((uint8_t)BE->getKind());
    // LHS, RHS references:
    SW.write((uint32_t)BC.exprTable_.getIdFor(BE->getLHS()));
    SW.write((uint32_t)BC.exprTable_.getIdFor(BE->getRHS()));
    return;
  }

  if (auto *UE = dynamic_cast<UnaryExpr *>(E)) {
    // Kind:
    SW.write((uint32_t)ExprTokenKind::UnaryExprKind);
    // My ID:
    SW.write((uint32_t)BC.exprTable_.getIdFor(UE));
    // OpKind:
    SW.write((uint8_t)UE->getKind());
    // Param reference references:
    SW.write((uint32_t)BC.exprTable_.getIdFor(UE->getVal()));
    return;
  }

  if (auto *LE = dynamic_cast<LoadExpr *>(E)) {
    // Kind:
    SW.write((uint32_t)ExprTokenKind::LoadExprKind);
    // My ID:
    SW.write((uint32_t)BC.exprTable_.getIdFor(LE));
    // Save the index of the argument that we are indexing.
    SW.write((uint32_t)p->getArgIndex(LE->getDest()));
    // Write the index to the expr load type.
    SW.write((uint32_t)BH.getExprTyTable().getIdFor(LE->getType()));
    // Write the number of subscript indices.
    SW.write((uint32_t)LE->getIndices().size());
    // Save the indices ids:
    for (auto &E : LE->getIndices()) {
      SW.write((uint32_t)BC.exprTable_.getIdFor(E.get()));
    }
    return;
  }

  if (auto *LL = dynamic_cast<LoadLocalExpr *>(E)) {
    // Kind:
    SW.write((uint32_t)ExprTokenKind::LoadLocalExprKind);
    // My ID:
    SW.write((uint32_t)BC.exprTable_.getIdFor(LL));
    // Save the index of the variable that we are indexing.
    SW.write((uint32_t)p->getVarIndex(LL->getDest()));
    return;
  }

  if (auto *BE = dynamic_cast<BroadcastExpr *>(E)) {
    // Kind:
    SW.write((uint32_t)ExprTokenKind::BroadcastExprKind);
    // My ID:
    SW.write((uint32_t)BC.exprTable_.getIdFor(BE));
    // Save the broadcasted value.
    SW.write((uint32_t)BC.exprTable_.getIdFor(BE->getValue()));
    // Save the vectorization factor.
    SW.write((uint8_t)BE->getVF());
    return;
  }
  if (auto *IE = dynamic_cast<IndexExpr *>(E)) {
    // Kind:
    SW.write((uint32_t)ExprTokenKind::IndexExprKind);
    // My ID:
    SW.write((uint32_t)BC.exprTable_.getIdFor(IE));
    // Save the broadcasted value.
    SW.write((uint32_t)BC.stmtTable_.getIdFor(IE->getLoop()));
    return;
  }

  assert(false);
}

void Bytecode::serialize(StreamWriter &SW, BytecodeHeader &BH,
                         SerializeContext &BC, Program *p, Stmt *S) {
  if (auto *L = dynamic_cast<Loop *>(S)) {
    // Kind:
    SW.write((uint32_t)StmtTokenKind::LoopKind);
    // My ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor(L));
    // Parent ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor((Stmt *)L->getParent()));
    // Write loop name string id.
    SW.write((uint32_t)BH.getStringTable().getIdFor(L->getName()));
    // Write loop end.
    SW.write((uint32_t)L->getEnd());
    // Write loop stride.
    SW.write((uint32_t)L->getStride());
    return;
  }
  if (auto *IR = dynamic_cast<IfRange *>(S)) {
    // Kind:
    SW.write((uint32_t)StmtTokenKind::IfRangeKind);
    // My ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor(IR));
    // Parent ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor((Stmt *)IR->getParent()));
    // Write the index to the if expression.
    SW.write((uint32_t)BC.exprTable_.getIdFor(IR->getIndex().get()));
    // Write range start.
    SW.write((uint32_t)IR->getRange().first);
    // Write range end.
    SW.write((uint32_t)IR->getRange().first);
    return;
  }
  if (auto *CS = dynamic_cast<CallStmt *>(S)) {
    // Kind:
    SW.write((uint32_t)StmtTokenKind::CallStmtKind);
    // My ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor(CS));
    // Parent ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor((Stmt *)CS->getParent()));
    // Save the index of the name of the callee.
    SW.write((uint32_t)BH.getStringTable().getIdFor(CS->getName()));
    // Write the number of parameters.
    SW.write((uint8_t)CS->getParams().size());
    // Save the indices ids:
    for (auto &E : CS->getParams()) {
      SW.write((uint32_t)BC.exprTable_.getIdFor(E.get()));
    }
    return;
  }
  if (auto *ST = dynamic_cast<StoreStmt *>(S)) {
    // Kind:
    SW.write((uint32_t)StmtTokenKind::StoreStmtKind);
    // My ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor(ST));
    // Parent ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor((Stmt *)ST->getParent()));
    // Save the index of the argument that we are indexing.
    SW.write((uint32_t)p->getArgIndex(ST->getDest()));
    // Write if this is a write-only or accumulator store.
    SW.write((uint8_t)ST->isAccumulate());
    // Write the index to the saved expression.
    SW.write((uint32_t)BC.exprTable_.getIdFor(ST->getValue().get()));
    // Write the number of subscript indices.
    SW.write((uint32_t)ST->getIndices().size());
    // Save the indices ids:
    for (auto &E : ST->getIndices()) {
      SW.write((uint32_t)BC.exprTable_.getIdFor(E.get()));
    }
    return;
  }
  if (auto *STL = dynamic_cast<StoreLocalStmt *>(S)) {
    // Kind:
    SW.write((uint32_t)StmtTokenKind::StoreStmtKind);
    // My ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor(STL));
    // Parent ID:
    SW.write((uint32_t)BC.stmtTable_.getIdFor((Stmt *)STL->getParent()));
    // Save the index of the variable that we are indexing.
    SW.write((uint32_t)p->getVarIndex(STL->getDest()));
    // Write if this is a write-only or accumulator store.
    SW.write((uint8_t)STL->isAccumulate());
    // Write the index to the saved expression.
    SW.write((uint32_t)BC.exprTable_.getIdFor(STL->getValue().get()));
    return;
  }
  assert(false);
}

void Bytecode::deserializeExpr(StreamReader &SR, BytecodeHeader &BH,
                               DeserializeContext &BC, Program *p) {
  auto loc = DebugLoc::npos();
  // Read the expr opcode.
  ExprTokenKind opcode = (ExprTokenKind)SR.readU32();
  assert(opcode < ExprTokenKind::LastExprKind && "Invalid token");

  // The expr ID (index into the expr table).
  auto exprId = SR.readU32();

  switch (opcode) {
  case ConstantExprKind:
    BC.registerExpr(exprId, new ConstantExpr(SR.readU32()));
    return;
  case ConstantFPExprKind:
    BC.registerExpr(exprId, new ConstantFPExpr(SR.readF32()));
    return;
  case ConstantStringExprKind: {
    // Read the string value.
    auto val = BH.getStringTable().getById(SR.readU32());
    BC.registerExpr(exprId, new ConstantStringExpr(val));
  }
  case BinaryExprKind: {
    // Read the operation kind.
    auto kind = SR.readU8();
    // Read the LHS, RHS.
    auto *L = BC.getExpr(SR.readU32());
    auto *R = BC.getExpr(SR.readU32());
    BC.registerExpr(exprId,
                    new BinaryExpr(L, R, (BinaryExpr::BinOpKind)kind, loc));
    return;
  }
  case UnaryExprKind: {
    // Read the operation kind.
    auto kind = SR.readU8();
    // Read the value operand:
    auto *V = BC.getExpr(SR.readU32());
    BC.registerExpr(exprId,
                    new UnaryExpr(V, (UnaryExpr::UnaryOpKind)kind, loc));
    return;
  }
  case LoadExprKind: {
    // Read the argument that we index.
    auto arg = p->getArg(SR.readU32());
    // Read the type that we load.
    auto exprTy = BH.getExprTyTable().getById(SR.readU32());
    // Read the indices, as a list of expression references.
    auto numIndices = SR.readU32();
    std::vector<Expr *> indices;
    for (int i = 0; i < numIndices; i++) {
      indices.push_back(BC.getExpr(SR.readU32()));
    }
    BC.registerExpr(exprId, new LoadExpr(arg, indices, exprTy, loc));
    return;
  }
  case LoadLocalExprKind: {
    // Read the var that we index.
    auto var = p->getVar(SR.readU32());
    BC.registerExpr(exprId, new LoadLocalExpr(var, loc));
    return;
  }
  case BroadcastExprKind: {
    // Read the variable to broadcast.
    auto *V = BC.getExpr(SR.readU32());
    // Read the vectorization factor.
    auto vf = SR.readU8();
    BC.registerExpr(exprId, new BroadcastExpr(V, vf));
    return;
  }
  case IndexExprKind: {
    // Load the IndexExpr. It has a loop and references the unserialized
    // statements, so register this node for resolving later once everything
    // is deserialized.
    auto loopId = SR.readU32();
    auto *LI = new IndexExpr(nullptr, loc);
    BC.registerExpr(exprId, LI);
    BC.registerResolveLater(LI, loopId);
    return;
  }
  case LastExprKind:
    assert(false && "Invalid opcode");
    return;
  }
  assert(false);
}

void Bytecode::deserializeStmt(StreamReader &SR, BytecodeHeader &BH,
                               DeserializeContext &BC, Program *p) {
  auto loc = DebugLoc::npos();
  // Read the Stmt opcode.
  StmtTokenKind opcode = (StmtTokenKind)SR.readU32();
  assert(opcode < StmtTokenKind::LastStmtKind && "Invalid token");

  // The stmt ID (index into the stmt table).
  auto stmtId = SR.readU32();
  // The parent ID (index into the stmt table).
  auto parentId = SR.readU32();
  // Find the parent that holds this stmt.
  Scope *parent = (Scope *)BC.getStmt(parentId);

  switch (opcode) {
  case LoopKind: {
    std::string name = BH.getStringTable().getById(SR.readU32());
    auto end = SR.readU32();
    auto stride = SR.readU32();
    auto *L = new Loop(name, loc, end, stride);
    parent->addStmt(L);
    BC.registerStmt(stmtId, L);
    return;
  }
  case IfRangeKind: {
    // Read the index of the index expr.
    auto idxVal = BC.getExpr(SR.readU32());
    // Load the start..end range.
    auto start = SR.readU32();
    auto end = SR.readU32();
    // Register the new If.
    auto *IR = new IfRange(idxVal, start, end, loc);
    parent->addStmt(IR);
    BC.registerStmt(stmtId, IR);
    return;
  }
  case CallStmtKind: {
    // Read the callee name.
    std::string name = BH.getStringTable().getById(SR.readU32());
    // Read the parameters, as a list of expression references.
    auto numParams = SR.readU8();
    std::vector<Expr *> params;
    for (int i = 0; i < numParams; i++) {
      params.push_back(BC.getExpr(SR.readU32()));
    }
    // Register the store.
    auto *cs = new CallStmt(name, params, loc);
    parent->addStmt(cs);
    BC.registerStmt(stmtId, cs);
    return;
  }
  case StoreStmtKind: {
    // Read the argument that we index.
    auto arg = p->getArg(SR.readU32());
    // Is this an accumulate store or write-only.
    bool accumulate = SR.readU8();
    // Read the index of the stored expr.
    auto storedVal = BC.getExpr(SR.readU32());
    // Read the indices, as a list of expression references.
    auto numIndices = SR.readU32();
    std::vector<Expr *> indices;
    for (int i = 0; i < numIndices; i++) {
      indices.push_back(BC.getExpr(SR.readU32()));
    }
    // Register the store.
    auto *st = new StoreStmt(arg, indices, storedVal, accumulate, loc);
    parent->addStmt(st);
    BC.registerStmt(stmtId, st);
    return;
  }
  case StoreLocalStmtKind: {
    // Read the argument that we index.
    auto var = p->getVar(SR.readU32());
    // Is this an accumulate store or write-only.
    bool accumulate = SR.readU8();
    // Read the index of the stored expr.
    auto storedVal = BC.getExpr(SR.readU32());
    // Register the store.
    auto *stl = new StoreLocalStmt(var, storedVal, accumulate, loc);
    parent->addStmt(stl);
    BC.registerStmt(stmtId, stl);
    return;
  }
  case LastStmtKind:
    assert(false);
    return;
  }

  assert(false);
}

std::string Bytecode::serialize(Program *p) {
  std::string body;
  std::string header;
  BytecodeHeader BH;
  StreamWriter SR(body);

  //----------- Serialize the program decl ----------------//

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

  //----------- Serialize the program body ----------------//
  SerializeContext BC;
  // The program is serialized as index zero (see deserializer).
  BC.stmtTable_.getIdFor(p);

  auto exprs = collectExprs(p);

  // Write the number of expressions:
  SR.write((uint32_t)exprs.size());
  // And the expressions:
  for (auto &e : exprs) {
    serialize(SR, BH, BC, p, e);
  }

  // Collect and serialize the stmts in the program.
  auto stmts = collectStmts(p);

  // Write the number of stmts (minus the program stmt):
  SR.write((uint32_t)stmts.size() - 1);
  // And the stmts:
  for (auto &s : stmts) {
    if (s == p)
      continue;
    serialize(SR, BH, BC, p, s);
  }

  // Return the complete serialized program.
  StreamWriter headerSR(header);
  BH.serialize(headerSR);
  return header + body;
}

Program *Bytecode::deserialize(const std::string &media) {
  BytecodeHeader BH;
  StreamReader SR(media);
  BH.deserialize(SR);

  //----------- Deserialize the program decl ----------------//

  // Read the function name.
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

  //----------- Deserialize the program body ----------------//
  DeserializeContext BC;
  // The program is serialized as index zero (see serializer).
  BC.registerStmt(0, p);

  // Read the number of expressions.
  auto numExprs = SR.readU32();
  // Read the expressions:
  for (int i = 0; i < numExprs; i++) {
    deserializeExpr(SR, BH, BC, p);
  }

  // Read the number of statements.
  auto numStmts = SR.readU32();
  // Read the statements:
  for (int i = 0; i < numStmts; i++) {
    deserializeStmt(SR, BH, BC, p);
  }

  // Resolve the loop indices.
  for (auto entry : BC.resolveLater_) {
    IndexExpr *IE = entry.first;
    Loop *L = dynamic_cast<Loop *>(BC.getStmt(entry.second));
    IE->setLoop(L);
  }

  return p;
}
