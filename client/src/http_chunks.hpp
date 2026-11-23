// Uvicorn pode usar chunked mesmo para o GET HTTP/1.0 do xsdk. Delimitadores nunca entram no anel MPEG.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace net {

class ChunkReader {
  enum State { SIZE, EXTENSION, LINE_END, DATA, DATA_CR, DATA_LF, END };
  State state = SIZE;
  uint64_t remaining = 0;
  unsigned digits = 0, line = 0;

public:
  bool finished() const { return state == END; }

  template<class Sink>
  bool feed(const uint8_t* p, size_t n, Sink&& put) {
    while (n && !finished()) {
      if (state == DATA) {
        size_t take = (size_t)std::min<uint64_t>(n, remaining);
        put(p, take);
        p += take; n -= take; remaining -= take;
        if (!remaining) state = DATA_CR;
        continue;
      }
      uint8_t c = *p++; --n;
      switch (state) {
        case SIZE:
          if (digits && (c == ';' || c == '\r')) {
            state = c == ';' ? EXTENSION : LINE_END;
          } else {
            int d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
            if (d < 0 || ++digits > 16) return false;
            remaining = (remaining << 4) | (unsigned)d;
          }
          break;
        case EXTENSION:
          if (++line > 1024) return false;
          if (c == '\r') state = LINE_END;
          else if (c < 32) return false;
          break;
        case LINE_END:
          if (c != '\n') return false;
          state = remaining ? DATA : END;
          break;
        case DATA_CR:
          if (c != '\r') return false;
          state = DATA_LF;
          break;
        case DATA_LF:
          if (c != '\n') return false;
          state = SIZE; digits = line = 0;
          break;
        default: return false;
      }
    }
    return true;
  }
};
}
