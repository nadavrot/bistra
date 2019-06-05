#include "bistra/Bytecode/Bytecode.h"
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

StreamReader::StreamReader(std::string &str) : stream_(str), pos_(0) {}

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

bool StreamReader::hasMore() { return pos_ != stream_.size(); }
