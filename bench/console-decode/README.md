# Decoder benchmark

`ytspike` is the original Xbox-only network and single-thread decoder test. `decode-quality` measures the player’s four-thread MPEG-1 decoder on the same MPEG-PS files on PC and Xbox. It loads the test files into RAM, disables audio decoding and leaves B-frame skipping off. The paired mode alternates blocks of 15 pictures in ABBA order so both encodes experience similar CPU load and clock speeds. The file limit is 64 MiB.

## PC

```sh
cmake -S bench/console-decode -B bench/console-decode/build -DCMAKE_BUILD_TYPE=Release
cmake --build bench/console-decode/build --target decode-quality
bench/console-decode/build/decode-quality --pair before.mpg after.mpg
```

Use the same scene, start time, duration and frame rate for both files. Compare `frames` and `mean_ms` on the `decode` lines. `p95_ms` and `max_ms` show slow frames. The additional PC `cpu_ms` is CPU time summed over the decoder threads per output frame.

## Xbox

With the same SDK and SDL-xenon installation as the client:

```sh
. "$HOME/.config/tube360/build.env"
cmake -S bench/console-decode -B .work/bench-xbox \
  -DCMAKE_TOOLCHAIN_FILE="$XSDK_DIR/cmake/xsdk.cmake" -DXSDK_TOOLCHAIN="$XSDK_TOOLCHAIN" \
  -DSDL3_DIR="$SDL3_XENON_DIR" -DTUBE360_QUALITY_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .work/bench-xbox --target decode-quality
```

Put `decode-quality.xex` in its own folder as `default.xex`, with `before.mpg` and `after.mpg` alongside it. Launch it from Aurora. Read both `decode` lines in `default.log`. Run twice, exchanging the file names for the second run. The player’s separate `decod:` log also measures playback under network, audio and rendering load.
