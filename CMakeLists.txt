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
  src/view/bench_script.cpp
  src/view/overlay.cpp
  src/library/exif.cpp
  src/library/catalog.cpp
  src/library/scanner.cpp
  src/platform/paths.cpp
  src/stream/job_system.cpp
  src/stream/slot_allocator.cpp
  src/stream/residency.cpp
  src/app/options.cpp
  src/app/config.cpp
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
  tests/test_catalog.cpp
  tests/test_scanner.cpp
  tests/test_jobs.cpp
  tests/test_residency.cpp
  tests/test_options.cpp
  tests/test_bench.cpp
  tests/test_overlay.cpp
  tests/test_config.cpp
)
target_include_directories(pb_tests PRIVATE third_party)
target_link_libraries(pb_tests PRIVATE pb_core)
target_compile_definitions(pb_tests PRIVATE PB_TEST_PHOTOS_DIR="${CMAKE_SOURCE_DIR}/photos")
add_test(NAME pb_tests COMMAND pb_tests)

# --- pb_imaging: JPEG codec, resizing, thumbnail cache files, indexer ---------
if(PB_HAVE_TURBOJPEG)
  add_library(pb_imaging STATIC
    src/imaging/jpeg.cpp
    src/imaging/resize.cpp
    src/cache/l0_pack.cpp
    src/cache/thumb_cache.cpp
    src/library/indexer.cpp
  )
  target_link_libraries(pb_imaging PUBLIC pb_core PRIVATE pb_turbojpeg)

  # pb_app: library indexing and streaming (no SDL / GL; talks to a TextureSink).
  add_library(pb_app STATIC
    src/app/library_index.cpp
    src/app/streamer.cpp
  )
  target_link_libraries(pb_app PUBLIC pb_imaging)
  target_sources(pb_tests PRIVATE
    tests/test_imaging.cpp
    tests/test_cache.cpp
    tests/test_indexer.cpp
    tests/test_library_index.cpp
    tests/test_streamer.cpp
  )
  target_link_libraries(pb_tests PRIVATE pb_app)
endif()

# --- pb_gfx + photobrowse executable ------------------------------------------
if(PB_HAVE_TURBOJPEG AND PB_HAVE_SDL3)
  add_library(pb_gfx STATIC
    src/gfx/gl.cpp
    src/gfx/shader.cpp
    src/gfx/paged_texture.cpp
    src/gfx/tile_renderer.cpp
    src/gfx/overlay_renderer.cpp
    src/gfx/background_renderer.cpp
  )
  target_include_directories(pb_gfx PUBLIC third_party/khronos)
  target_link_libraries(pb_gfx PUBLIC pb_imaging)
  pb_embed_shaders(pb_gfx "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders_embedded.h"
    "${CMAKE_SOURCE_DIR}/shaders/tile.vert"
    "${CMAKE_SOURCE_DIR}/shaders/tile.frag"
    "${CMAKE_SOURCE_DIR}/shaders/overlay.vert"
    "${CMAKE_SOURCE_DIR}/shaders/overlay.frag"
    "${CMAKE_SOURCE_DIR}/shaders/background.vert"
    "${CMAKE_SOURCE_DIR}/shaders/background.frag"
  )

  add_executable(photobrowse
    src/main.cpp
    src/app/app.cpp
    src/app/gpu_textures.cpp
  )
  target_link_libraries(photobrowse PRIVATE pb_app pb_gfx SDL3::SDL3)
endif()
