#pragma once
#include "FS.h"
#include "SPI.h"
class SDFS : public fs::FS {
 public:
  bool begin(uint8_t = 5, SPIClass& = SPI, uint32_t = 4000000, const char* = "/sd", uint8_t = 5, bool = false) { return true; }
  uint64_t cardSize() { return 16ULL << 30; }
  uint64_t totalBytes() { return 15ULL << 30; }
  uint64_t usedBytes() { return 3ULL << 30; }
  int cardType() { return 2; }
  void end() {}
};
extern SDFS SD;
