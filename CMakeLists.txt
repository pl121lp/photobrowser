cmake_minimum_required(VERSION 3.24)
project(PhotoBrowse VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/EmbedShaders.cmake)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

# --- Dependencies (never installed by the build; see README) -----------------
find_package(SDL3 CONFIG QUIET)
set(PB_HAVE_SDL3 ${SDL3_FOUND})

set(PB_HAVE_TURBOJPEG OFF)
find_package(libjpeg-turbo CONFIG QUIET)
if(TARGET libjpeg-turbo::turbojpeg)
  add_library(pb_turbojpeg INTERFACE)
  target_link_libraries(pb_turbojpeg INTERFACE libjpeg-turbo::turbojpeg)
  set(PB_HAVE_TURBOJPEG ON)
elseif(TARGET libjpeg-turbo::turbojpeg-static)
  add_library(pb_turbojpeg INTERFACE)
  target_link_libraries(pb_turbojpeg INTERFACE libjpeg-turbo::turbojpeg-static)
  set(PB_HAVE_TURBOJPEG ON)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(TURBOJPEG QUIET IMPORTED_TARGET libturbojpeg)
    if(TURBOJPEG_FOUND)
      add_library(pb_turbojpeg INTERFACE)
      target_link_libraries(pb_turbojpeg INTERFACE PkgConfig::TURBOJPEG)
      set(PB_HAVE_TURBOJPEG ON)
    endif()
  endif()
endif()

if(NOT PB_HAVE_TURBOJPEG)
  message(WARNING "libjpeg-turbo (TurboJPEG) not found: building core library and tests only.")
endif()
if(NOT PB_HAVE_SDL3)
  message(WARNING "SDL3 not found: the photobrowse executable will not be built.")
endif()

find_package(Threads REQUIRED)

# --- pb_core: pure logic, no SDL / GL / codec dependencies -------------------
add_library(pb_core STATIC
  src/layout/content.cpp
  src/view/camera.cpp
  src/view/motion.cpp
  src/view/interaction.cpp
  src/library/exif.cpp
  src/layout/square_grid.cpp
)
target_include_directories(pb_core PUBLIC src)
target_link_libraries(pb_core PUBLIC Threads::Threads)

# --- Tests ------------------------------------------------------------------
enable_testing()
add_executable(pb_tests
  tests/test_main.cpp
  tests/test_geom.cpp
  tests/test_layout.cpp
  tests/test_camera.cpp
  tests/test_motion.cpp
  tests/test_interaction.cpp
  tests/test_exif.cpp
)
target_include_directories(pb_tests PRIVATE third_party)
target_link_libraries(pb_tests PRIVATE pb_core)
target_compile_definitions(pb_tests PRIVATE PB_TEST_PHOTOS_DIR="${CMAKE_SOURCE_DIR}/photos")
add_test(NAME pb_tests COMMAND pb_tests)
