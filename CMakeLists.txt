cmake_minimum_required(VERSION 3.25)

set(CMAKE_C_COMPILER clang)
project(periferi LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_definitions(_GNU_SOURCE)
add_compile_options(-Wall -Wextra -Wpedantic)
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")

# Vendored TOML parser: not our code, so no warnings from it.
add_library(tomlc17 STATIC vendor/tomlc17/tomlc17.c)
target_include_directories(tomlc17 PUBLIC vendor/tomlc17)
target_compile_options(tomlc17 PRIVATE -w)

find_package(ALSA REQUIRED)

add_executable(peri  src/peri.c src/udev.c src/json.c src/module.c src/fsutil.c src/xdg.c src/service.c src/request.c src/scan.c src/xap.c src/razer.c src/razer_scan.c src/razer_dev.c src/razer90.c src/razer90_dev.c src/camera.c)
add_executable(perid src/perid.c src/udev.c src/inventory.c src/module.c src/fsutil.c src/xdg.c src/request.c src/rgb_cmd.c src/rgb_settings.c src/scan.c src/xap.c src/device_cmd.c src/audio.c src/store.c src/controls.c src/follow.c src/razer.c src/razer_dev.c src/razer90.c src/razer90_dev.c src/camera.c)
target_link_libraries(peri PRIVATE tomlc17)
target_link_libraries(perid PRIVATE tomlc17 ALSA::ALSA)

install(TARGETS peri perid RUNTIME DESTINATION bin)
install(DIRECTORY catalog/ DESTINATION share/peri/catalog)

enable_testing()
add_executable(color_test tests/color_test.c)
add_test(NAME color COMMAND color_test)

add_executable(scan_test tests/scan_test.c src/scan.c)
target_compile_definitions(scan_test PRIVATE FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/descriptors")
add_test(NAME scan COMMAND scan_test)

add_executable(inventory_test tests/inventory_test.c src/inventory.c src/module.c src/fsutil.c src/xdg.c src/scan.c)
target_link_libraries(inventory_test PRIVATE tomlc17)
add_test(NAME inventory COMMAND inventory_test)

add_executable(udev_test tests/udev_test.c src/udev.c src/module.c src/fsutil.c src/xdg.c)
target_link_libraries(udev_test PRIVATE tomlc17)
add_test(NAME udev COMMAND udev_test)

add_executable(rgb_settings_test tests/rgb_settings_test.c src/rgb_settings.c src/xap.c src/scan.c)
add_test(NAME rgb_settings COMMAND rgb_settings_test)

add_executable(razer_test tests/razer_test.c src/razer.c)
add_test(NAME razer COMMAND razer_test)

add_executable(camera_test tests/camera_test.c src/camera.c)
add_test(NAME camera COMMAND camera_test)

add_test(NAME camera_no_streaming
         COMMAND ${CMAKE_COMMAND} -DFILE=${CMAKE_SOURCE_DIR}/src/camera.c -P ${CMAKE_SOURCE_DIR}/cmake/check_camera.cmake)

add_executable(razer90_test tests/razer90_test.c src/razer90.c src/razer.c)
add_test(NAME razer90 COMMAND razer90_test)

add_executable(store_test tests/store_test.c src/store.c src/module.c src/fsutil.c src/xdg.c src/razer.c)
target_link_libraries(store_test PRIVATE tomlc17)
add_test(NAME store COMMAND store_test)

add_executable(audio_test tests/audio_test.c src/audio.c)
target_link_libraries(audio_test PRIVATE ALSA::ALSA)
add_test(NAME audio COMMAND audio_test)

add_executable(razer_scan_test tests/razer_scan_test.c src/razer_scan.c src/razer.c)
add_test(NAME razer_scan COMMAND razer_scan_test)

add_executable(follow_test tests/follow_test.c src/follow.c)
add_test(NAME follow COMMAND follow_test)

add_executable(json_test tests/json_test.c src/json.c)
add_test(NAME json COMMAND json_test)

add_executable(module_test tests/module_test.c src/module.c src/fsutil.c src/xdg.c)
target_link_libraries(module_test PRIVATE tomlc17)
target_compile_definitions(module_test PRIVATE
  CATALOG_DIR="${CMAKE_SOURCE_DIR}/catalog"
  FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/modules")
add_test(NAME module COMMAND module_test)

add_executable(xdg_test tests/xdg_test.c src/xdg.c)
add_test(NAME xdg COMMAND xdg_test)

add_executable(service_test tests/service_test.c src/service.c)
add_test(NAME service COMMAND service_test)

add_executable(request_test tests/request_test.c src/request.c)
add_test(NAME request COMMAND request_test)

add_executable(fsutil_test tests/fsutil_test.c src/fsutil.c)
add_test(NAME fsutil COMMAND fsutil_test)

# Refusals only: the cases below return before a device is opened. The sources are
# the ones set_handle already needs; the test does not call a path that writes.
add_executable(set_test tests/set_test.c src/device_cmd.c src/audio.c src/store.c src/controls.c
               src/follow.c src/razer.c src/razer_dev.c src/razer90.c src/razer90_dev.c src/camera.c
               src/module.c src/fsutil.c src/xdg.c src/scan.c)
target_link_libraries(set_test PRIVATE tomlc17 ALSA::ALSA)
add_test(NAME set COMMAND set_test)

# The camera pane's core against a fake peri, headless (no window, no device): needs quickshell.
find_program(QUICKSHELL quickshell)
if(QUICKSHELL)
  add_test(NAME camera_pane_scenario COMMAND ${CMAKE_SOURCE_DIR}/tests/qml/camera-scenario/run.sh)
  # About 20 s: `ctest -LE scenario` skips it, `ctest -L scenario` runs only it.
  set_tests_properties(camera_pane_scenario PROPERTIES LABELS scenario TIMEOUT 120)
  add_test(NAME mouse_pane_scenario COMMAND ${CMAKE_SOURCE_DIR}/tests/qml/mouse-scenario/run.sh)
  set_tests_properties(mouse_pane_scenario PROPERTIES LABELS scenario TIMEOUT 120)
  add_test(NAME lighting_pane_scenario COMMAND ${CMAKE_SOURCE_DIR}/tests/qml/lighting-scenario/run.sh)
  set_tests_properties(lighting_pane_scenario PROPERTIES LABELS scenario TIMEOUT 120)
  add_test(NAME mic_pane_scenario COMMAND ${CMAKE_SOURCE_DIR}/tests/qml/mic-scenario/run.sh)
  set_tests_properties(mic_pane_scenario PROPERTIES LABELS scenario TIMEOUT 120)
else()
  message(WARNING "quickshell not found: the camera pane scenario is skipped")
endif()

# QML logic tests need the Qt 6 runner (/usr/bin/qmltestrunner is Qt 5 on this system).
# The pass pattern demands at least one passed test, so an empty or mistyped run cannot pass.
find_program(QMLTESTRUNNER NAMES qmltestrunner qmltestrunner6 PATHS /usr/lib/qt6/bin NO_DEFAULT_PATH)
if(QMLTESTRUNNER)
  add_test(NAME qml COMMAND ${QMLTESTRUNNER} -input ${CMAKE_SOURCE_DIR}/tests/qml)
  set_tests_properties(qml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    PASS_REGULAR_EXPRESSION "Totals: [1-9][0-9]* passed, 0 failed")
else()
  message(WARNING "qmltestrunner (Qt 6) not found: QML tests skipped")
endif()
