#include "bistra/Bytecode/Bytecode.h"
#include "bistra/Program/Program.h"
#include "bistra/Program/Utils.h"

#include "gtest/gtest.h"

using namespace bistra;

TEST(basic, string_tables) {
  Bytecode BC;
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
