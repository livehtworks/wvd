# 仅显式 M2 离线目标加载 SDK。M1 automationd 不链接、不发现设备、不开放运行接口。
file(READ "${PROJECT_SOURCE_DIR}/.local/maafw.json" WVD_MAA_CONFIG)
string(JSON WVD_MAA_SDK GET "${WVD_MAA_CONFIG}" sdk)
string(JSON WVD_MAA_ARCHIVE GET "${WVD_MAA_CONFIG}" sdk_archive)
file(SHA256 "${WVD_MAA_ARCHIVE}" WVD_MAA_ARCHIVE_SHA)
if(NOT WVD_MAA_ARCHIVE_SHA STREQUAL "179b51b6211fc74317c8dc06f10fae4724aca370df7d1bf694cd00d06ff38efe")
  message(FATAL_ERROR "Maa SDK archive does not match locked 5.13.0")
endif()
string(JSON WVD_MAA_FILE_COUNT LENGTH "${WVD_MAA_CONFIG}" sdk_files)
math(EXPR WVD_MAA_LAST "${WVD_MAA_FILE_COUNT} - 1")
foreach(INDEX RANGE ${WVD_MAA_LAST})
  string(JSON RELATIVE MEMBER "${WVD_MAA_CONFIG}" sdk_files ${INDEX})
  string(JSON EXPECTED GET "${WVD_MAA_CONFIG}" sdk_files "${RELATIVE}")
  file(SHA256 "${WVD_MAA_SDK}/${RELATIVE}" ACTUAL)
  if(NOT ACTUAL STREQUAL EXPECTED)
    message(FATAL_ERROR "Prepared Maa SDK changed: ${RELATIVE}")
  endif()
endforeach()
# 构建身份绑定实际编译源码与锁文件，不能用回调地址或调用者随意声称的版本代替。
file(GLOB_RECURSE WVD_ID_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/native/*.cpp" "${PROJECT_SOURCE_DIR}/native/*.hpp" "${PROJECT_SOURCE_DIR}/tests/native/*.cpp" "${PROJECT_SOURCE_DIR}/tests/native/*.hpp")
list(APPEND WVD_ID_SOURCES "${PROJECT_SOURCE_DIR}/dependencies.lock.json" "${CMAKE_CURRENT_LIST_FILE}")
if(EXISTS "${PROJECT_SOURCE_DIR}/opencv.lock.json")
  list(APPEND WVD_ID_SOURCES "${PROJECT_SOURCE_DIR}/opencv.lock.json")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WVD_ID_SOURCES})
list(SORT WVD_ID_SOURCES)
set(WVD_ID_INPUT "")
foreach(SOURCE IN LISTS WVD_ID_SOURCES)
  file(SHA256 "${SOURCE}" SOURCE_HASH)
  file(RELATIVE_PATH SOURCE_NAME "${PROJECT_SOURCE_DIR}" "${SOURCE}")
  string(APPEND WVD_ID_INPUT "${SOURCE_NAME}:${SOURCE_HASH}\n")
endforeach()
string(SHA256 WVD_BUILD_ID "${WVD_ID_INPUT}")
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/generated/core_build_id.hpp" CONTENT "#pragma once\n#define WVD_CORE_BUILD_ID \"@WVD_BUILD_ID@\"\n" @ONLY)
add_library(wvd_core recognition.cpp preflight.cpp gateway.cpp guarded_controller.cpp
  ../platform/windows/file_digest.cpp ../platform/windows/runtime_files.cpp
  ../storage/run_store.cpp ../storage/runtime_bundle.cpp ../storage/pipeline_bundle.cpp ../platform/windows/bundle_lease.cpp
  ../devices/input_gate.cpp ../devices/lifecycle_execution.cpp ../runtime/guarded_action.cpp
  ../runtime/execution_session.cpp ../runtime/run_coordinator.cpp ../runtime/behavior_registry.cpp)
target_include_directories(wvd_core PUBLIC "${PROJECT_SOURCE_DIR}/native" "${PROJECT_BINARY_DIR}/generated")
target_include_directories(wvd_core PUBLIC "${WVD_MAA_SDK}/include")
target_compile_definitions(wvd_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(wvd_core PRIVATE /utf-8 /W4 /EHsc)
target_link_libraries(wvd_core PRIVATE "${WVD_MAA_SDK}/lib/MaaFramework.lib" bcrypt ole32)
add_executable(test_recognition "${PROJECT_SOURCE_DIR}/tests/native/test_recognition.cpp")
target_include_directories(test_recognition PRIVATE "${PROJECT_BINARY_DIR}/generated" "${WVD_MAA_SDK}/include")
target_compile_options(test_recognition PRIVATE /utf-8 /W4 /EHsc)
target_link_libraries(test_recognition PRIVATE wvd_core)
target_link_options(test_recognition PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/native/platform/windows/app.manifest")
set_target_properties(test_recognition PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/m2")
add_executable(test_runtime "${PROJECT_SOURCE_DIR}/tests/native/test_runtime.cpp"
  "${PROJECT_SOURCE_DIR}/tests/native/runtime_cases.cpp" "${PROJECT_SOURCE_DIR}/tests/native/gate_cases.cpp")
target_link_libraries(test_runtime PRIVATE wvd_core)
target_compile_options(test_runtime PRIVATE /utf-8 /W4 /EHsc)
target_link_options(test_runtime PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/native/platform/windows/app.manifest")
set_target_properties(test_runtime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/m2")
if(WVD_BUILD_M4)
  add_library(wvd_business ../games/wvd/profile.cpp ../games/wvd/tasks/quest_catalog.cpp ../games/wvd/tasks/task_plan.cpp
    ../games/wvd/mining/progress.cpp
    ../games/wvd/tasks/mining.cpp
    ../games/wvd/tasks/manual_separation.cpp
    ../games/wvd/tasks/bounty_visit.cpp
    ../games/wvd/quests/manual_separation.cpp
    ../games/wvd/quests/sleep_visits.cpp
    ../games/wvd/tasks/sleep_visits.cpp
    ../games/wvd/tasks/featured_request.cpp
    ../games/wvd/tasks/golden_chest.cpp
    ../games/wvd/tasks/sandman.cpp
    ../games/wvd/quests/bounty_cycle.cpp
    ../games/wvd/tasks/bounty_cycle.cpp
    ../games/wvd/tasks/fishing.cpp
    ../games/wvd/tasks/fishing_supply.cpp
    ../games/wvd/fishing/progress.cpp
    ../games/wvd/tasks/pipeline_compiler.cpp ../games/wvd/navigation/world_map.cpp ../games/wvd/supply/inn.cpp
    ../games/wvd/tasks/dungeon_route.cpp
    ../games/wvd/tasks/departure.cpp
    ../games/wvd/tasks/dungeon_iteration.cpp
    ../games/wvd/navigation/map_route.cpp
    ../games/wvd/navigation/auto_route.cpp
    ../games/wvd/navigation/time_leap.cpp
    ../games/wvd/navigation/dungeon_entry.cpp
    ../games/wvd/navigation/world_travel.cpp ../games/wvd/supply/party.cpp
    ../games/wvd/diagnostics.cpp
    ../games/wvd/chest/chest.cpp
    ../games/wvd/chest/selection.cpp ../games/wvd/chest/character.cpp
    ../games/wvd/supply/policy.cpp
    ../games/wvd/supply/dungeon_recover.cpp
    ../games/wvd/combat/auto_combat.cpp
    ../games/wvd/combat/turn.cpp
    ../games/wvd/combat/encounter.cpp
    ../games/wvd/recovery/boot.cpp
    ../games/wvd/recovery/revival.cpp
    ../games/wvd/recovery/party_death.cpp
    ../games/wvd/recovery/global_prompt.cpp
    ../games/wvd/recovery/dialogue.cpp
    ../games/wvd/navigation/wall_bypass.cpp
    ../games/wvd/state.cpp ../games/wvd/state_factory.cpp ../games/wvd/karma.cpp ../games/wvd/tasks/fortress_trap.cpp
    ../games/wvd/tasks/giant.cpp
    ../games/wvd/tasks/dark_light.cpp
    ../games/wvd/recovery/karma_prompt.cpp ../storage/karma_writer.cpp
    ../games/wvd/combat/strategy.cpp ../storage/legacy_import.cpp ../storage/profile_store.cpp)
  target_link_libraries(wvd_business PUBLIC wvd_core)
  target_include_directories(wvd_business SYSTEM PRIVATE "${BOOST_ROOT}")
  target_compile_options(wvd_business PRIVATE /utf-8 /W4 /EHsc)
  add_executable(wvd_m4_check ../app/m4_check.cpp)
  target_link_libraries(wvd_m4_check PRIVATE wvd_business)
  target_compile_options(wvd_m4_check PRIVATE /utf-8 /W4 /EHsc)
  target_link_options(wvd_m4_check PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/native/platform/windows/app.manifest")
  set_target_properties(wvd_m4_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/m4")
  add_executable(test_m4_state "${PROJECT_SOURCE_DIR}/tests/native/test_m4_state.cpp")
  target_link_libraries(test_m4_state PRIVATE wvd_business)
  target_compile_options(test_m4_state PRIVATE /utf-8 /W4 /EHsc)
  target_link_options(test_m4_state PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/native/platform/windows/app.manifest")
  set_target_properties(test_m4_state PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/m4")
endif()
if(WVD_BUILD_M3)
  file(READ "${PROJECT_SOURCE_DIR}/.local/opencv.json" OPENCV_CONFIG)
  string(JSON OPENCV_ROOT GET "${OPENCV_CONFIG}" root)
  file(READ "${PROJECT_SOURCE_DIR}/opencv.lock.json" OPENCV_LOCK)
  string(JSON OPENCV_COUNT LENGTH "${OPENCV_LOCK}" files)
  math(EXPR OPENCV_LAST "${OPENCV_COUNT}-1")
  foreach(INDEX RANGE ${OPENCV_LAST})
    string(JSON RELATIVE MEMBER "${OPENCV_LOCK}" files ${INDEX})
    string(JSON EXPECTED GET "${OPENCV_LOCK}" files "${RELATIVE}")
    file(SHA256 "${OPENCV_ROOT}/${RELATIVE}" ACTUAL)
    if(NOT ACTUAL STREQUAL EXPECTED)
      message(FATAL_ERROR "OpenCV dependency changed: ${RELATIVE}")
    endif()
  endforeach()
  file(SHA256 "${WVD_MAA_SDK}/bin/opencv_world4_maa.dll" SDK_OPENCV_SHA)
  string(JSON OPENCV_DLL_SHA GET "${OPENCV_LOCK}" dll_sha256)
  if(NOT SDK_OPENCV_SHA STREQUAL OPENCV_DLL_SHA)
    message(FATAL_ERROR "OpenCV does not match Maa SDK DLL")
  endif()
  add_library(wvd_vision ../games/wvd/vision/asset_resolver.cpp ../games/wvd/vision/recognizers.cpp ../games/wvd/vision/bobber.cpp
    ../games/wvd/vision/unknown_window.cpp)
  target_include_directories(wvd_vision PUBLIC "${OPENCV_ROOT}/include/opencv4")
  target_link_libraries(wvd_vision PUBLIC wvd_core "${OPENCV_ROOT}/lib/opencv_world4.lib")
  target_compile_options(wvd_vision PRIVATE /utf-8 /W4 /EHsc)
  if(WVD_BUILD_M4)
    target_sources(wvd_business PRIVATE ../games/wvd/tasks/workflow_session.cpp)
    target_link_libraries(wvd_business PUBLIC wvd_vision)
    add_executable(test_m4_workflow "${PROJECT_SOURCE_DIR}/tests/native/test_m4_workflow.cpp")
    target_link_libraries(test_m4_workflow PRIVATE wvd_business wvd_vision)
    target_compile_options(test_m4_workflow PRIVATE /utf-8 /W4 /EHsc)
    target_link_options(test_m4_workflow PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/native/platform/windows/app.manifest")
    set_target_properties(test_m4_workflow PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/m4")
  endif()
  add_library(wvd_device adb_backend.cpp ../platform/windows/mumu_binding.cpp ../platform/windows/metadata_query.cpp)
  target_link_libraries(wvd_device PUBLIC wvd_core)
  target_compile_options(wvd_device PRIVATE /utf-8 /W4 /EHsc)
  add_executable(wvd_m3_check ../app/m3_check.cpp)
  target_link_libraries(wvd_m3_check PRIVATE wvd_vision wvd_device)
  target_compile_options(wvd_m3_check PRIVATE /utf-8 /W4 /EHsc)
  target_link_options(wvd_m3_check PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/native/platform/windows/app.manifest")
  set_target_properties(wvd_m3_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/m3")
  add_executable(test_m3 "${PROJECT_SOURCE_DIR}/tests/native/test_m3.cpp" "${PROJECT_SOURCE_DIR}/tests/native/m3_device_cases.cpp")
  target_link_libraries(test_m3 PRIVATE wvd_vision wvd_device)
  target_compile_options(test_m3 PRIVATE /utf-8 /W4 /EHsc)
  target_link_options(test_m3 PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/native/platform/windows/app.manifest")
  set_target_properties(test_m3 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/m3")
  add_executable(wvd_metadata_helper "${PROJECT_SOURCE_DIR}/tests/native/metadata_helper.cpp")
  target_compile_definitions(wvd_metadata_helper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  add_executable(test_metadata "${PROJECT_SOURCE_DIR}/tests/native/metadata_cases.cpp")
  target_link_libraries(test_metadata PRIVATE wvd_device)
  add_executable(test_m3_fixes "${PROJECT_SOURCE_DIR}/tests/native/test_m3_fixes.cpp")
  target_link_libraries(test_m3_fixes PRIVATE wvd_vision)
  foreach(TARGET wvd_metadata_helper test_metadata test_m3_fixes)
    target_compile_options(${TARGET} PRIVATE /utf-8 /W4 /EHsc)
    target_link_options(${TARGET} PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/native/platform/windows/app.manifest")
    set_target_properties(${TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/m3")
  endforeach()
endif()
