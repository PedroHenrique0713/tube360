# Tube360 no PC (Fedora): mesmo código do console, com janela do SDL e Vulkan da Mesa, para testar a interface.
#   cmake -S client -B client/build && cmake --build client/build
# O build do console fica em x360/.
cmake_minimum_required(VERSION 3.20)
project(tube360 CXX C)
find_package(SDL3 REQUIRED CONFIG)
find_package(Vulkan REQUIRED)
set(CMAKE_CXX_STANDARD 20)
set(SHADERS ui.vert shape.frag image.frag text.frag video.frag)
set(SHADER_HEADERS)
foreach(s ${SHADERS})
  string(REPLACE "." "_" var ${s})
  set(out ${CMAKE_CURRENT_BINARY_DIR}/${s}.h)
  add_custom_command(OUTPUT ${out} COMMAND glslangValidator -V --vn ${var} -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${s}
                     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${s})
  list(APPEND SHADER_HEADERS ${out})
endforeach()
add_executable(tube360 src/main.cpp ${SHADER_HEADERS})
target_include_directories(tube360 PRIVATE ${CMAKE_CURRENT_BINARY_DIR} src third_party)
target_compile_options(tube360 PRIVATE -O2 -Wall -Wno-missing-field-initializers)
target_link_libraries(tube360 PRIVATE SDL3::SDL3 Vulkan::Vulkan)
include(ffmpeg.cmake)
