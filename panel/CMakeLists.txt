cmake_minimum_required(VERSION 3.16)
project(frameeyeosc-panel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The panel ships in the same tarball as frameeyeosc, so it takes its version from Cargo.toml
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../Cargo.toml" CARGO_VERSION_LINE REGEX "^version = \"[^\"]+\"" LIMIT_COUNT 1)
string(REGEX REPLACE "^version = \"([^\"]+)\".*" "\\1" FRAMEEYEOSC_VERSION "${CARGO_VERSION_LINE}")
if(NOT FRAMEEYEOSC_VERSION)
  message(FATAL_ERROR "Could not read the version from ../Cargo.toml")
endif()
# The update checker shared with the other Frame apps (copied by frame-updater's sync.sh)
set(FRAME_UPDATER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/frame-updater")

# The OpenVR library that ships with SteamVR (its path on the Frame's SteamOS)
set(OPENVR_LIB_DIR "/opt/steamvr/bin/linuxarm64" CACHE PATH "Directory containing libopenvr_api.so")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan)

add_executable(frameeyeosc-panel
  src/main.cpp
  src/autostart.cpp
  src/command.cpp
  src/config.cpp
  src/draw.cpp
  src/gaze_dots.cpp
  src/gaze_fit.cpp
  src/i18n.cpp
  src/json.cpp
  src/model.cpp
  src/panel.cpp
  src/sounds.cpp
  src/status.cpp
  src/target.cpp
  src/theme.cpp
  src/vk_texture.cpp
  src/vr_overlay.cpp
  ${FRAME_UPDATER_DIR}/cpp/update_check.cpp
)
target_include_directories(frameeyeosc-panel PRIVATE third_party/openvr ${FRAME_UPDATER_DIR}/cpp)
target_compile_definitions(frameeyeosc-panel PRIVATE FRAMEEYEOSC_VERSION="${FRAMEEYEOSC_VERSION}")
target_compile_options(frameeyeosc-panel PRIVATE -Wall -Wextra)
target_link_libraries(frameeyeosc-panel PRIVATE
  PkgConfig::CAIRO
  PkgConfig::FREETYPE
  PkgConfig::VULKAN
  Threads::Threads
  "${OPENVR_LIB_DIR}/libopenvr_api.so"
)
# Use SteamVR's own library at run time too
set_target_properties(frameeyeosc-panel PROPERTIES
  BUILD_RPATH "${OPENVR_LIB_DIR}"
  INSTALL_RPATH "${OPENVR_LIB_DIR}"
)

install(TARGETS frameeyeosc-panel RUNTIME DESTINATION bin)

# The gaze fit's logic, tested without OpenVR: panel/build/gaze-fit-test (or ctest)
add_executable(gaze-fit-test tests/gaze_fit_test.cpp src/gaze_fit.cpp src/status.cpp src/json.cpp)
target_include_directories(gaze-fit-test PRIVATE src)
target_compile_options(gaze-fit-test PRIVATE -Wall -Wextra)
enable_testing()
add_test(NAME gaze-fit COMMAND gaze-fit-test)

# The debug gaze dots' packet, geometry and socket, tested without OpenVR: panel/build/gaze-dots-test
add_executable(gaze-dots-test tests/gaze_dots_test.cpp src/gaze_dots.cpp src/gaze_fit.cpp src/status.cpp src/json.cpp)
target_include_directories(gaze-dots-test PRIVATE src)
target_compile_options(gaze-dots-test PRIVATE -Wall -Wextra)
add_test(NAME gaze-dots COMMAND gaze-dots-test)

# The eye fit's sound cues (the WAV files and which cue goes with which moment), without playing: panel/build/sounds-test
add_executable(sounds-test tests/sounds_test.cpp src/sounds.cpp src/gaze_fit.cpp src/status.cpp src/json.cpp)
target_include_directories(sounds-test PRIVATE src)
target_compile_options(sounds-test PRIVATE -Wall -Wextra)
add_test(NAME sounds COMMAND sounds-test)
