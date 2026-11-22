cmake_minimum_required(VERSION 3.25)
project(ytspike C)

add_executable(ytspike main.c)
set_target_properties(ytspike PROPERTIES C_STANDARD 17 C_EXTENSIONS ON)
target_include_directories(ytspike PRIVATE vendor)
target_compile_options(ytspike PRIVATE -O2 -Wall -Wextra)
xsdk_add_xex(ytspike)
