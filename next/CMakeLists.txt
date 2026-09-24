cmake_minimum_required(VERSION 3.24)
project(wvd_next VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT WIN32)
  message(FATAL_ERROR "Native WVD Next currently supports Windows only")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.local/dependencies.json" WVD_DEPS)
string(JSON BOOST_ROOT GET "${WVD_DEPS}" boost)
string(JSON JSON_HEADER GET "${WVD_DEPS}" json)
file(SHA256 "${JSON_HEADER}" JSON_SHA)
if(NOT JSON_SHA STREQUAL "9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6")
  message(FATAL_ERROR "Locked JSON header checksum mismatch")
endif()
configure_file("${JSON_HEADER}" "${CMAKE_CURRENT_BINARY_DIR}/generated/json.hpp" COPYONLY)
add_library(wvd_http native/api/http_server.cpp native/api/routes.cpp)
target_include_directories(wvd_http PUBLIC native PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(wvd_http SYSTEM PUBLIC "${BOOST_ROOT}")
target_compile_definitions(wvd_http PUBLIC BOOST_ERROR_CODE_HEADER_ONLY NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(wvd_http PRIVATE /utf-8 /W4 /EHsc)
target_link_libraries(wvd_http PUBLIC ws2_32 mswsock)
add_subdirectory(native)
set(WVD_NATIVE_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/.local/native-deps")
set(WVD_OPENCV "${WVD_NATIVE_DEPS}/opencv-unpacked/opencv/build")
set(WVD_ORT "${WVD_NATIVE_DEPS}/ort-unpacked/onnxruntime-win-x64-1.22.1")
add_executable(automationd native/app/main.cpp)
target_link_libraries(automationd PRIVATE wvd_native_application)
target_compile_options(automationd PRIVATE /utf-8 /W4 /EHsc)
target_link_options(automationd PRIVATE "/MANIFEST:EMBED"
  "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/native/platform/windows/app.manifest")
add_dependencies(automationd wvd-capture-host)
add_custom_command(TARGET automationd POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${WVD_ORT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:automationd>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${WVD_OPENCV}/x64/vc16/bin/opencv_world4120.dll" "$<TARGET_FILE_DIR:automationd>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${WVD_NATIVE_DEPS}/archives/scrcpy-server-v3.3.4" "$<TARGET_FILE_DIR:automationd>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:wvd-capture-host>" "$<TARGET_FILE_DIR:automationd>")
add_executable(test_head_response tests/native/test_head_response.cpp)
target_link_libraries(test_head_response PRIVATE wvd_http)
target_compile_options(test_head_response PRIVATE /utf-8 /W4 /EHsc)
