GNU FriBidi 1.0.16, Unicode 16.0.0 (LGPL-2.1-or-later).

Source: https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz
SHA-256: 1b1cde5b235d40479e91be2f0e88a309e3214c8ab470ec8a2744d82a5a9ea05c

The C/headers/generated Unicode tables from upstream lib/ are unmodified.
Tube360 supplies fribidi-config.h and client/text.cmake.
Only the bidi, joining, mirroring, shaping and run core is linked; legacy charset
conversion and deprecated entry-point implementations are not linked.

The full source archive additionally contains the Unicode table generators and
upstream conformance tests. No source download or generated-table build is needed
for either the desktop or Xbox build. Keep COPYING and the matching static library
with the release/relink kit; see docs/RELEASE.md and THIRD_PARTY.md.
