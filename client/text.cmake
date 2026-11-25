# Unicode bidi e formas árabes; sem charset legado nem dependência de ICU/HarfBuzz.
set(FRIBIDI_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/fribidi)
add_library(tube360-text STATIC
  ${FRIBIDI_DIR}/fribidi.c ${FRIBIDI_DIR}/fribidi-arabic.c
  ${FRIBIDI_DIR}/fribidi-bidi.c ${FRIBIDI_DIR}/fribidi-bidi-types.c
  ${FRIBIDI_DIR}/fribidi-brackets.c ${FRIBIDI_DIR}/fribidi-joining.c
  ${FRIBIDI_DIR}/fribidi-joining-types.c ${FRIBIDI_DIR}/fribidi-mirroring.c
  ${FRIBIDI_DIR}/fribidi-run.c ${FRIBIDI_DIR}/fribidi-shape.c)
target_include_directories(tube360-text PUBLIC ${FRIBIDI_DIR})
target_compile_definitions(tube360-text PRIVATE FRIBIDI_BUILD HAVE_STDLIB_H=1
  HAVE_STRING_H=1 STDC_HEADERS=1 HAVE_STRINGIZE=1)
target_compile_options(tube360-text PRIVATE -O2)
