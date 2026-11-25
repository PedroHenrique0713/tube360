# Tube360 para o console: o mesmo src/main.cpp, ligado ao vulkan4xenon e ao SDL-xenon.
# Use tools/build-xbox.sh (gera os shaders do Xenos e chama o CMake com o toolchain do xsdk).
cmake_minimum_required(VERSION 3.25)
project(tube360_x360 C CXX)
find_package(SDL3 REQUIRED CONFIG)
set(V4X ${V4X_DIR})
add_executable(tube360 ../src/main.cpp shaders_bundle.c)
option(TUBE360_F33_BENCH "Enable the opt-in self-terminating 1080p playback probe" OFF)
if(TUBE360_F33_BENCH)
  target_compile_definitions(tube360 PRIVATE TUBE360_F33_BENCH=1)
endif()
target_compile_features(tube360 PRIVATE cxx_std_20)
target_compile_options(tube360 PRIVATE -O2 -Wall -Wno-missing-field-initializers)
target_include_directories(tube360 PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../src ${CMAKE_CURRENT_LIST_DIR}/../third_party
  ${V4X}/include ${V4X}/third_party/vulkan-headers/include)
target_link_libraries(tube360 PRIVATE ${V4X}/build/libvulkan4xenon.a SDL3::SDL3-static)
set_property(TARGET tube360 APPEND PROPERTY LINK_DEPENDS ${V4X}/build/libvulkan4xenon.a)
target_sources(tube360 PRIVATE ffmpeg-libc.c player-fault.c)
target_link_options(tube360 PRIVATE -Wl,--wrap=pthread_create)
include(../ffmpeg.cmake)
include(../youtube.cmake)
# title ID próprio ("XTUB") e o nome que o Aurora mostra
set(APP_NAME "Tube360" CACHE STRING "Nome que o Aurora e o painel mostram")
xsdk_add_xex(tube360 TITLE_ID 0x58545542 NAME "${APP_NAME}" VERSION 1.0.0.0)

add_executable(tube360-launcher ../src/update/launcher.cpp ../src/update/launcher_x360.cpp)
target_link_libraries(tube360-launcher PRIVATE tube360-update-core)
xsdk_add_xex(tube360-launcher TITLE_ID 0x58545542 NAME "${APP_NAME}" VERSION 1.0.0.0)
