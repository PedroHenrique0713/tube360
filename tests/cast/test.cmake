# Mesmo main/player/render do produto, com os dois transportes substituídos no link por loopback.
add_executable(nest-startup-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/nest_startup.cpp")
target_include_directories(nest-startup-test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../client/src"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/third_party")
target_link_libraries(nest-startup-test PRIVATE tube360-cast tube360-yt tube360-ffmpeg SDL3::SDL3)

add_executable(member-playback-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/members.cpp")
target_include_directories(member-playback-test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../client/src"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/third_party")
target_link_libraries(member-playback-test PRIVATE tube360-cast tube360-yt tube360-ffmpeg SDL3::SDL3)

add_executable(cast-test-app EXCLUDE_FROM_ALL
  $<TARGET_OBJECTS:tube360-main>
  "${CMAKE_CURRENT_LIST_DIR}/../../client/src/cast/discovery.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/src/cast/lounge.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/src/cast/receiver.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/transport.cpp")
target_compile_features(cast-test-app PRIVATE cxx_std_20)
target_compile_definitions(cast-test-app PRIVATE TUBE360_CAST_TEST=1 PICOJSON_USE_LOCALE=0)
target_include_directories(cast-test-app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/src" "${CMAKE_CURRENT_LIST_DIR}/../../client/third_party")
target_compile_options(cast-test-app PRIVATE -O2 -Wall -Wno-missing-field-initializers)
target_link_options(cast-test-app PRIVATE -Wl,--wrap=_ZN2yt5httpsEPKSt6atomicIbE)
target_link_libraries(cast-test-app PRIVATE tube360-update tube360-yt tube360-ffmpeg CURL::libcurl SDL3::SDL3 Vulkan::Vulkan)
add_executable(cast-protocol-test EXCLUDE_FROM_ALL
  "${CMAKE_CURRENT_LIST_DIR}/protocol.cpp" "${CMAKE_CURRENT_LIST_DIR}/../../client/src/cast/lounge.cpp")
target_compile_features(cast-protocol-test PRIVATE cxx_std_17)
target_include_directories(cast-protocol-test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../client/src"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/third_party")
target_link_libraries(cast-protocol-test PRIVATE SDL3::SDL3)
add_executable(nest-live-clock-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/../nest/live_clock.cpp")
target_include_directories(nest-live-clock-test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../client/src"
  "${CMAKE_CURRENT_LIST_DIR}/../../client/third_party")
target_link_libraries(nest-live-clock-test PRIVATE tube360-yt tube360-ffmpeg SDL3::SDL3)
if(NOT XSDK AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(cast-lan-test EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/lan_faults.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/transport.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../../client/src/cast/receiver.cpp")
  target_include_directories(cast-lan-test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../client/src"
    "${CMAKE_CURRENT_LIST_DIR}/../../client/third_party")
  target_link_libraries(cast-lan-test PRIVATE tube360-cast tube360-yt SDL3::SDL3)
  target_link_options(cast-lan-test PRIVATE -Wl,--wrap=getsockname -Wl,--wrap=bind
    -Wl,--wrap=setsockopt -Wl,--wrap=sendto)
  add_executable(nest-scan-fault-test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/../nest/discovery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../nest/socket_faults.cpp")
  target_include_directories(nest-scan-fault-test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../client/src"
    "${CMAKE_CURRENT_LIST_DIR}/../../client/third_party")
  target_compile_options(nest-scan-fault-test PRIVATE -UNDEBUG)
  target_link_libraries(nest-scan-fault-test PRIVATE tube360-cast tube360-yt SDL3::SDL3)
  target_link_options(nest-scan-fault-test PRIVATE -Wl,--wrap=connect -Wl,--wrap=select -Wl,--wrap=close)
endif()
