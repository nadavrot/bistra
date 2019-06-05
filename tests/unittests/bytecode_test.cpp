#include "bistra/Bytecode/Bytecode.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include "gtest/gtest.h"

using namespace bistra;

TEST(basic, string_tables) {
  BytecodeHeader BC;
  auto &ST = BC.getStringTable();
  auto a1 = ST.getIdFor("hello");
  auto a2 = ST.getIdFor("world");
  auto a3 = ST.getIdFor("hello");
  auto a4 = ST.getIdFor("world");
  auto a5 = ST.getIdFor("types");
  EXPECT_EQ(a1, 0);
  EXPECT_EQ(a2, 1);
  EXPECT_EQ(a3, 0);
  EXPECT_EQ(a4, 1);
  EXPECT_EQ(a5, 2);
  auto s1 = ST.getById(0);
  auto s2 = ST.getById(1);
  auto s3 = ST.getById(2);
  EXPECT_EQ(s1, "hello");
  EXPECT_EQ(s2, "world");
  EXPECT_EQ(s3, "types");
}

TEST(basic, test_streams) {
  std::string back;
  StreamWriter SW(back);
  StreamReader SR(back);

  SW.write((uint32_t)0x11223344);
  SW.write((std::string) "hello");
  SW.write((uint32_t)54321);
  SW.write((uint32_t)0);
  SW.write((std::string) "hello");
  SW.write((std::string) "");
  SW.write((uint8_t)17);
  SW.write((std::string) "");
  SW.write((uint32_t)12345);
  SW.write((std::string) "");
  SW.write((uint32_t)0x11223344);
  SW.write((std::string) "hello");

  EXPECT_EQ(SR.hasMore(), true);
  EXPECT_EQ(SR.readU32(), 0x11223344);
  EXPECT_EQ(SR.readStr(), "hello");
  EXPECT_EQ(SR.readU32(), 54321);
  EXPECT_EQ(SR.readU32(), 0);
  EXPECT_EQ(SR.readStr(), "hello");
  EXPECT_EQ(SR.readStr(), "");
  EXPECT_EQ(SR.hasMore(), true);
  EXPECT_EQ(SR.readU8(), 17);
  EXPECT_EQ(SR.readStr(), "");
  EXPECT_EQ(SR.readU32(), 12345);
  EXPECT_EQ(SR.readStr(), "");
  EXPECT_EQ(SR.readU32(), 0x11223344);
  EXPECT_EQ(SR.readStr(), "hello");
  EXPECT_EQ(SR.hasMore(), false);
}

TEST(basic, serialize_header) {
  // Searialize oneBC into media and read into twoBC.
  BytecodeHeader oneBC;
  BytecodeHeader twoBC;
  std::string media;

  // Generate data for oneBC.
  {
    auto &ST = oneBC.getStringTable();
    ST.getIdFor("hello");
    ST.getIdFor("world");
    ST.getIdFor("");
    ST.getIdFor("world");
    ST.getIdFor("types");
    EXPECT_EQ(ST.size(), 4);

    auto &ET = oneBC.getExprTyTable();
    ET.getIdFor(ExprType(ElemKind::Float32Ty));
    ET.getIdFor(ExprType(ElemKind::Int8Ty, 8));
    ET.getIdFor(ExprType(ElemKind::Float32Ty, 4));
    EXPECT_EQ(ET.size(), 3);

    Type T1(ElemKind::Float32Ty, {4}, {"I"});
    Type T2(ElemKind::Float32Ty, {4, 5, 6}, {"A", "B", "C"});
    Type T3(ElemKind::Float32Ty, {4, 5, 6, 1, 1}, {"A", "B", "C", "", "R"});

    auto &TT = oneBC.getTensorTypeTable();
    TT.getIdFor(T1);
    TT.getIdFor(T2);
    TT.getIdFor(T3);
    EXPECT_EQ(TT.size(), 3);
  }

  StreamWriter SW(media);
  StreamReader SR(media);
  oneBC.serialize(SW);
  twoBC.deserialize(SR);

  // Compare oneBC to twoBC.
  {
    auto &ST1 = oneBC.getStringTable();
    auto &ST2 = twoBC.getStringTable();
    EXPECT_EQ(ST1.size(), ST2.size());

    auto &ET1 = oneBC.getExprTyTable();
    auto &ET2 = twoBC.getExprTyTable();
    EXPECT_EQ(ET1.size(), ET2.size());

    auto &TT1 = oneBC.getTensorTypeTable();
    auto &TT2 = twoBC.getTensorTypeTable();
    EXPECT_EQ(TT1.size(), TT2.size());

    for (auto &S : ST1.get()) {
      EXPECT_EQ(ST1.getIdFor(S), ST2.getIdFor(S));
    }
    for (auto &E : ET1.get()) {
      EXPECT_EQ(ET1.getIdFor(E), ET2.getIdFor(E));
    }
    for (auto &T : TT1.get()) {
      EXPECT_EQ(TT1.getIdFor(T), TT2.getIdFor(T));
    }
  }
}

TEST(basic, serialize_program) {
  auto loc = DebugLoc::npos();
  Program *p = new Program("memset", loc);
  p->addArgument("DEST", {125}, {"len"}, ElemKind::Float32Ty);
  p->addLocalVar("local", ExprType(ElemKind::Float32Ty, 4));

  auto media = Bytecode::serialize(p);
  Program *dp = Bytecode::deserialize(media);

  dp->dump();
  EXPECT_EQ(dp->getName(), p->getName());
  EXPECT_EQ(dp->getVars().size(), p->getVars().size());
  EXPECT_EQ(dp->getArgs().size(), p->getArgs().size());
}
