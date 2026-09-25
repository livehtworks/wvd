# 活动产品的唯一原生构建图；所有运行目标都不得链接已归档的 SDK。
set(WVD_NATIVE_DEPS "${PROJECT_SOURCE_DIR}/.local/native-deps")
set(WVD_OPENCV "${WVD_NATIVE_DEPS}/opencv-unpacked/opencv/build")
set(WVD_ORT "${WVD_NATIVE_DEPS}/ort-unpacked/onnxruntime-win-x64-1.22.1")
set(WVD_RAPID "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rapidocr_core")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/resources/authoring/semantic-assets.json")
file(READ "${PROJECT_SOURCE_DIR}/resources/authoring/semantic-assets.json" WVD_SEMANTIC_CATALOGUE)
file(SHA256 "${PROJECT_SOURCE_DIR}/resources/authoring/semantic-assets.json" WVD_SEMANTIC_SOURCE_SHA256)
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
file(WRITE "${PROJECT_BINARY_DIR}/generated/semantic_catalogue.sha256" "${WVD_SEMANTIC_SOURCE_SHA256}\n")
set(WVD_PROBE_RESOURCES "")
foreach(ID IN ITEMS city.royal.identity city.inn.entry city.temple.entry
  city.blacksmith.entry city.ruins.entry city.guild.entry city.ore_merchant.entry
  city.item_shop.entry city.edge.entry city.any
  guild.commissions.page guild.bounties.page chest.open.option chest.choose.page
  chest.reward.page)
  string(JSON ENTRY GET "${WVD_SEMANTIC_CATALOGUE}" resources "${ID}")
  if(NOT WVD_PROBE_RESOURCES STREQUAL "")
    string(APPEND WVD_PROBE_RESOURCES ",")
  endif()
  string(APPEND WVD_PROBE_RESOURCES "\"${ID}\":${ENTRY}")
endforeach()
set(WVD_SEMANTIC_CATALOGUE "{\"schema\":1,\"resources\":{${WVD_PROBE_RESOURCES}}}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/games/wvd/vision/semantic_catalogue.hpp.in"
  "${PROJECT_BINARY_DIR}/generated/semantic_catalogue.hpp" @ONLY)
file(READ "${PROJECT_SOURCE_DIR}/native-dependencies.lock.json" WVD_NATIVE_LOCK)
foreach(INDEX RANGE 0 2)
  string(JSON NAME GET "${WVD_NATIVE_LOCK}" binaries ${INDEX} filename)
  string(JSON EXPECTED GET "${WVD_NATIVE_LOCK}" binaries ${INDEX} sha256)
  if(NOT EXISTS "${WVD_NATIVE_DEPS}/archives/${NAME}")
    message(FATAL_ERROR "Missing locked native dependency: ${NAME}")
  endif()
  file(SHA256 "${WVD_NATIVE_DEPS}/archives/${NAME}" ACTUAL)
  if(NOT ACTUAL STREQUAL EXPECTED)
    message(FATAL_ERROR "Native dependency checksum mismatch: ${NAME}")
  endif()
endforeach()

add_library(wvd_native_rapid STATIC
  ../third_party/rapidocr_core/src/DbNet.cpp
  ../third_party/rapidocr_core/src/CrnnNet.cpp
  ../third_party/rapidocr_core/src/AngleNet.cpp
  ../third_party/rapidocr_core/src/OcrUtils.cpp
  ../third_party/rapidocr_core/src/OcrResultUtils.cpp
  ../third_party/rapidocr_core/src/clipper.cpp
  ../third_party/rapidocr_core/src/OcrLiteImpl.cpp
  ../third_party/rapidocr_core/src/OcrLite.cpp)
target_include_directories(wvd_native_rapid PRIVATE "${WVD_RAPID}/include"
  "${WVD_OPENCV}/include" "${WVD_ORT}/include")
target_compile_options(wvd_native_rapid PRIVATE /utf-8 /EHsc)

add_library(wvd_native_platform STATIC
  platform/windows/process.cpp platform/windows/file_digest.cpp
  platform/windows/runtime_files.cpp platform/windows/bundle_lease.cpp
  platform/windows/mumu_binding.cpp platform/windows/metadata_query.cpp
  platform/windows/memory_diagnostics.cpp)
target_include_directories(wvd_native_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(wvd_native_platform PUBLIC bcrypt ole32 psapi)
target_compile_options(wvd_native_platform PRIVATE /utf-8 /W4 /EHsc)

add_library(wvd_native_recognition STATIC
  recognition/request.cpp recognition/frame.cpp recognition/ocr.cpp recognition/service.cpp
  recognition/resources.cpp)
target_include_directories(wvd_native_recognition PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_BINARY_DIR}/generated" "${WVD_OPENCV}/include")
target_include_directories(wvd_native_recognition PRIVATE "${WVD_RAPID}/include"
  "${WVD_ORT}/include")
target_link_libraries(wvd_native_recognition PUBLIC wvd_native_platform
  "${WVD_OPENCV}/x64/vc16/lib/opencv_world4120.lib"
  "${WVD_ORT}/lib/onnxruntime.lib" wvd_native_rapid)
target_compile_options(wvd_native_recognition PRIVATE /utf-8 /W4 /EHsc)

add_library(wvd_native_workflow STATIC workflow/program.cpp workflow/serialization.cpp)
# 流程表示/序列化只依赖轻量契约，不通过此目标把 ORT/OpenCV 泄漏给全部编译消费者。
target_include_directories(wvd_native_workflow PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_BINARY_DIR}/generated")
target_compile_options(wvd_native_workflow PRIVATE /utf-8 /W4 /EHsc)

add_library(wvd_native_devices STATIC devices/adb_command.cpp devices/mumu_capture.cpp
  devices/device_session.cpp devices/native_input_gate.cpp devices/scrcpy_codec.cpp
  devices/scrcpy_control.cpp devices/lifecycle_execution.cpp)
target_link_libraries(wvd_native_devices PUBLIC wvd_native_platform
  wvd_native_recognition ws2_32)
target_compile_options(wvd_native_devices PRIVATE /utf-8 /W4 /EHsc)

add_library(wvd_native_games STATIC
  games/wvd/tasks/handoff_provenance.cpp games/wvd/state.cpp
  games/wvd/recovery/leap_wait_state.cpp games/wvd/combat/turn.cpp
  games/wvd/combat/auto_combat.cpp games/wvd/chest/chest.cpp
  games/wvd/chest/selection.cpp games/wvd/native_operations.cpp
  games/wvd/tasks/native_program.cpp games/wvd/tasks/native_publisher.cpp
  games/wvd/vision/native_asset_resolver.cpp games/wvd/vision/native_recognizers.cpp
  games/wvd/vision/bobber.cpp games/wvd/vision/unknown_window.cpp)
target_sources(wvd_native_games PRIVATE
  games/wvd/profile.cpp games/wvd/tasks/quest_catalog.cpp
  games/wvd/tasks/task_plan.cpp games/wvd/mining/progress.cpp
  games/wvd/tasks/mining.cpp games/wvd/tasks/manual_separation.cpp
  games/wvd/tasks/run_builder.cpp
  games/wvd/tasks/bounty_visit.cpp games/wvd/quests/manual_separation.cpp
  games/wvd/quests/sleep_visits.cpp games/wvd/tasks/sleep_visits.cpp
  games/wvd/tasks/featured_request.cpp games/wvd/tasks/golden_chest.cpp
  games/wvd/tasks/sandman.cpp games/wvd/tasks/gold_income.cpp
  games/wvd/tasks/bull_cave.cpp games/wvd/tasks/steel_trial.cpp
  games/wvd/tasks/repel_forces.cpp games/wvd/tasks/fordraig.cpp
  games/wvd/tasks/cave_of_separation.cpp games/wvd/navigation/causality.cpp
  games/wvd/navigation/return_city.cpp games/wvd/navigation/harken_exit.cpp
  games/wvd/quests/bounty_cycle.cpp games/wvd/tasks/bounty_cycle.cpp
  games/wvd/tasks/fishing.cpp games/wvd/tasks/fishing_supply.cpp
  games/wvd/fishing/progress.cpp games/wvd/tasks/pipeline_compiler.cpp games/wvd/tasks/locale_assets.cpp
  games/wvd/tasks/author_workflow.cpp authoring/workflow_validator.cpp
  storage/workflow_repository.cpp
  games/wvd/navigation/world_map.cpp games/wvd/supply/inn.cpp
  games/wvd/tasks/dungeon_route.cpp games/wvd/tasks/departure.cpp
  games/wvd/tasks/dungeon_iteration.cpp games/wvd/navigation/map_route.cpp
  games/wvd/navigation/auto_route.cpp games/wvd/navigation/time_leap.cpp
  games/wvd/navigation/dungeon_entry.cpp games/wvd/navigation/world_travel.cpp
  games/wvd/supply/party.cpp games/wvd/supply/policy.cpp
  games/wvd/supply/dungeon_recover.cpp games/wvd/combat/encounter.cpp
  games/wvd/recovery/revival.cpp games/wvd/recovery/party_death.cpp
  games/wvd/recovery/boot.cpp
  games/wvd/recovery/global_prompt.cpp games/wvd/recovery/dialogue.cpp
  games/wvd/navigation/wall_bypass.cpp games/wvd/karma.cpp
  games/wvd/tasks/fortress_trap.cpp games/wvd/tasks/giant.cpp
  games/wvd/tasks/dark_light.cpp games/wvd/recovery/karma_prompt.cpp
  storage/karma_writer.cpp games/wvd/combat/strategy.cpp
  storage/legacy_import.cpp storage/profile_store.cpp)
target_link_libraries(wvd_native_games PUBLIC wvd_native_workflow wvd_native_platform wvd_native_recognition psapi)
target_include_directories(wvd_native_games SYSTEM PRIVATE "${BOOST_ROOT}")
target_compile_options(wvd_native_games PRIVATE /utf-8 /W4 /EHsc)
target_compile_definitions(wvd_native_games PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

add_library(wvd_native_runtime STATIC runtime/flow_executor.cpp
  runtime/flow_executor_events.cpp
  runtime/native_flow_ports.cpp runtime/native_execution_session.cpp
  runtime/native_run_coordinator.cpp storage/run_store.cpp)
target_link_libraries(wvd_native_runtime PUBLIC wvd_native_workflow
  wvd_native_devices)
target_compile_options(wvd_native_runtime PRIVATE /utf-8 /W4 /EHsc)
target_compile_definitions(wvd_native_runtime PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(wvd-capture-host devices/capture_host.cpp)
target_include_directories(wvd-capture-host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(wvd-capture-host PRIVATE /utf-8 /W4 /EHsc)

add_executable(test_native_flow EXCLUDE_FROM_ALL ../tests/native/test_flow_executor.cpp)
target_link_libraries(test_native_flow PRIVATE wvd_native_runtime)
target_compile_options(test_native_flow PRIVATE /utf-8 /W4 /EHsc)
add_executable(test_native_devices EXCLUDE_FROM_ALL
  ../tests/native/test_native_devices.cpp)
target_link_libraries(test_native_devices PRIVATE wvd_native_devices)
target_compile_options(test_native_devices PRIVATE /utf-8 /W4 /EHsc)
add_executable(capture_stall_helper EXCLUDE_FROM_ALL
  ../tests/native/capture_stall_helper.cpp)
target_compile_options(capture_stall_helper PRIVATE /utf-8 /W4 /EHsc)
add_executable(test_native_capture EXCLUDE_FROM_ALL
  ../tests/native/test_native_capture.cpp)
target_link_libraries(test_native_capture PRIVATE wvd_native_devices)
target_compile_options(test_native_capture PRIVATE /utf-8 /W4 /EHsc)
add_executable(test_native_author EXCLUDE_FROM_ALL
  ../tests/native/test_native_author.cpp)
target_link_libraries(test_native_author PRIVATE wvd_native_games)
target_compile_options(test_native_author PRIVATE /utf-8 /W4 /EHsc)
add_executable(test_native_coordinator EXCLUDE_FROM_ALL
  ../tests/native/test_native_coordinator.cpp)
target_link_libraries(test_native_coordinator PRIVATE wvd_native_runtime)
target_compile_options(test_native_coordinator PRIVATE /utf-8 /W4 /EHsc)
add_custom_command(TARGET test_native_coordinator POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${WVD_ORT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:test_native_coordinator>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${WVD_OPENCV}/x64/vc16/bin/opencv_world4120.dll"
    "$<TARGET_FILE_DIR:test_native_coordinator>")

add_library(wvd_native_application STATIC EXCLUDE_FROM_ALL app/application.cpp)
target_include_directories(wvd_native_application SYSTEM PRIVATE "${BOOST_ROOT}")
target_link_libraries(wvd_native_application PUBLIC wvd_http wvd_native_runtime
  wvd_native_games PRIVATE shell32 comdlg32)
target_compile_options(wvd_native_application PRIVATE /utf-8 /W4 /EHsc)
target_compile_definitions(wvd_native_application PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(test_native_application EXCLUDE_FROM_ALL
  ../tests/native/test_native_application.cpp)
target_link_libraries(test_native_application PRIVATE wvd_native_application)
target_compile_options(test_native_application PRIVATE /utf-8 /W4 /EHsc)
add_custom_command(TARGET test_native_application POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${WVD_ORT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:test_native_application>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${WVD_OPENCV}/x64/vc16/bin/opencv_world4120.dll"
    "$<TARGET_FILE_DIR:test_native_application>")

foreach(TEST_TARGET IN ITEMS test_native_recognition test_native_ocr)
  add_executable(${TEST_TARGET} EXCLUDE_FROM_ALL
    "../tests/native/${TEST_TARGET}.cpp")
  target_link_libraries(${TEST_TARGET} PRIVATE wvd_native_recognition)
  target_compile_options(${TEST_TARGET} PRIVATE /utf-8 /W4 /EHsc)
  add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${WVD_ORT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:${TEST_TARGET}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${WVD_OPENCV}/x64/vc16/bin/opencv_world4120.dll"
      "$<TARGET_FILE_DIR:${TEST_TARGET}>")
endforeach()
