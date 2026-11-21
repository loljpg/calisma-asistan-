#pragma once
#include <Arduino.h>
struct MP3FrameInfo { int bitrate, nChans, samprate, bitsPerSample, outputSamps, layer, version; };
namespace libhelix {
typedef void (*MP3DataCallback)(MP3FrameInfo& info, short* pcm_buffer, size_t len, void* ref);
class MP3DecoderHelix {
 public:
  MP3DecoderHelix(MP3DataCallback) {}
  void begin() {}
  void end() {}
  size_t write(const void*, size_t n) { return n; }
};
}
