# Mesmo main/player/render do produto, com os dois transportes substituídos no link por loopback.
add_executable(cast-test-app EXCLUDE_FROM_ALL
  $<TARGET_OBJECTS:tube360-main>
  "${CMAKE_CURRENT_LIST_DIR}/../../client/src/cast/lounge.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/src/cast/receiver.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/transport.cpp")
target_compile_features(cast-test-app PRIVATE cxx_std_20)
target_compile_definitions(cast-test-app PRIVATE TUBE360_CAST_TEST=1 PICOJSON_USE_LOCALE=0)
target_include_directories(cast-test-app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/src" "${CMAKE_CURRENT_LIST_DIR}/../../client/third_party")
target_compile_options(cast-test-app PRIVATE -O2 -Wall -Wno-missing-field-initializers)
target_link_libraries(cast-test-app PRIVATE tube360-update tube360-cast tube360-yt tube360-ffmpeg SDL3::SDL3 Vulkan::Vulkan)
add_executable(cast-protocol-test EXCLUDE_FROM_ALL
  "${CMAKE_CURRENT_LIST_DIR}/protocol.cpp" "${CMAKE_CURRENT_LIST_DIR}/../../client/src/cast/lounge.cpp")
target_compile_features(cast-protocol-test PRIVATE cxx_std_17)
target_include_directories(cast-protocol-test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../client/src"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/third_party")
target_link_libraries(cast-protocol-test PRIVATE SDL3::SDL3)
