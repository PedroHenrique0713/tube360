#pragma once
#include <cstdint>
#include <memory>

namespace video {
// A posse inclui DPB, fila e as duas fences de desenho; só volta ao pool depois dos três.
struct Surface {
  int w = 0, h = 0, alignedW = 0, alignedH = 0;
  uint8_t* data[3] = {};
  int stride[3] = {};
  bool submitted = false;
  virtual void publish() {}
  virtual ~Surface() = default;
};
struct Allocator {
  virtual std::shared_ptr<Surface> acquire(int w, int h, int alignedW, int alignedH, const int* alignment) = 0;
  virtual ~Allocator() = default;
};
}
