add_library(tube360-cast STATIC
  "${CMAKE_CURRENT_LIST_DIR}/src/cast/lounge.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/cast/receiver.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/src/cast/stream.cpp")
target_compile_features(tube360-cast PUBLIC cxx_std_17)
target_compile_options(tube360-cast PRIVATE -Wall -Wextra)
target_compile_definitions(tube360-cast PRIVATE PICOJSON_USE_LOCALE=0)
target_include_directories(tube360-cast PUBLIC "${CMAKE_CURRENT_LIST_DIR}/src" PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/third_party" "${CMAKE_CURRENT_BINARY_DIR}/yt")
target_link_libraries(tube360-cast PRIVATE tube360-yt CURL::libcurl SDL3::SDL3)
target_link_libraries(tube360 PRIVATE tube360-cast)
