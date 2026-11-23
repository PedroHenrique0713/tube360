#pragma once

namespace playback {
enum Mode { AUTO, FAST, DETAIL, COUNT };
inline int ceiling(int mode) { return mode == FAST ? 720 : 1080; }
inline int rate(int mode) { return mode == DETAIL ? 30 : 60; }
}
