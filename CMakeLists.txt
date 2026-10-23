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
  src/view/screensaver.cpp
  src/view/overlay.cpp
  src/library/exif.cpp
  src/library/catalog.cpp
  src/library/scanner.cpp
  src/library/media_format.cpp
  src/platform/paths.cpp
  src/platform/dynlib.cpp
  src/plugin/plugin_host.cpp
  src/stream/job_system.cpp
  src/stream/slot_allocator.cpp
  src/stream/residency.cpp
  src/app/options.cpp
  src/app/config.cpp
  src/app/background_presets.cpp
  src/layout/square_grid.cpp
  src/map/mercator.cpp
  src/map/map_camera.cpp
  src/map/fly_path.cpp
  src/map/tile_set.cpp
  src/map/map_view.cpp
)
target_include_directories(pb_core PUBLIC src)
file(GLOB PB_BACKGROUND_SHADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/shaders/backgrounds/*.frag")
pb_embed_shaders(pb_core "${CMAKE_CURRENT_BINARY_DIR}/generated/core/background_shaders.h" ${PB_BACKGROUND_SHADERS})
target_link_libraries(pb_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# Plugins are shared libraries in <build>/plugins (next to the exe's plugins/ folder
# when installed). They include only src/plugin/photobrowse_plugin.h and never link
# against the application.
set(PB_PLUGIN_DIR "${CMAKE_BINARY_DIR}/plugins")
add_custom_target(photobrowse_plugins)  # builds every enabled plugin
function(pb_add_plugin target name)
  add_library(${target} MODULE ${ARGN})
  target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/src")
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "pb_${name}"
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${PB_PLUGIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${PB_PLUGIN_DIR}")
  if(WIN32)
    set_target_properties(${target} PROPERTIES PREFIX "")
  else()
    set_target_properties(${target} PROPERTIES PREFIX "lib")
  endif()
endfunction()

# --- Tests ------------------------------------------------------------------
enable_testing()
add_executable(pb_tests
  tests/test_main.cpp
  tests/test_geom.cpp
  tests/test_layout.cpp
  tests/test_camera.cpp
  tests/test_motion.cpp
  tests/test_interaction.cpp
  tests/test_screensaver.cpp
  tests/test_exif.cpp
  tests/test_catalog.cpp
  tests/test_scanner.cpp
  tests/test_jobs.cpp
  tests/test_residency.cpp
  tests/test_options.cpp
  tests/test_bench.cpp
  tests/test_overlay.cpp
  tests/test_config.cpp
  tests/test_background.cpp
  tests/test_plugins.cpp
  tests/test_mercator.cpp
  tests/test_map_camera.cpp
  tests/test_fly_path.cpp
  tests/test_tile_set.cpp
  tests/test_map_view.cpp
  tests/test_iso6709.cpp
)
target_include_directories(pb_tests PRIVATE third_party)
target_link_libraries(pb_tests PRIVATE pb_core)
target_compile_definitions(pb_tests PRIVATE PB_TEST_PHOTOS_DIR="${CMAKE_SOURCE_DIR}/photos"
                                           PB_TEST_PLUGIN_DIR="${PB_PLUGIN_DIR}")
pb_add_plugin(pb_test_plugin testfake tests/plugins/test_plugin.c)
pb_add_plugin(pb_test_plugin_v1 testfake_v1 tests/plugins/test_plugin.c)  # version-1-sized table
target_compile_definitions(pb_test_plugin_v1 PRIVATE PB_TEST_V1_SIZE)
add_dependencies(pb_tests pb_test_plugin pb_test_plugin_v1)
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
    src/app/map_tiles.cpp
  )
  target_link_libraries(pb_app PUBLIC pb_imaging)
  target_sources(pb_tests PRIVATE
    tests/test_imaging.cpp
    tests/test_cache.cpp
    tests/test_indexer.cpp
    tests/test_library_index.cpp
    tests/test_streamer.cpp
    tests/test_map_tiles.cpp
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
    src/gfx/map_renderer.cpp
  )
  target_include_directories(pb_gfx PUBLIC third_party/khronos)
  target_link_libraries(pb_gfx PUBLIC pb_imaging)
  pb_embed_shaders(pb_gfx "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders_embedded.h"
    "${CMAKE_SOURCE_DIR}/shaders/tile.vert"
    "${CMAKE_SOURCE_DIR}/shaders/tile.frag"
    "${CMAKE_SOURCE_DIR}/shaders/overlay.vert"
    "${CMAKE_SOURCE_DIR}/shaders/overlay.frag"
    "${CMAKE_SOURCE_DIR}/shaders/fullscreen.vert"
    "${CMAKE_SOURCE_DIR}/shaders/map_dot.vert"
    "${CMAKE_SOURCE_DIR}/shaders/map_dot.frag"
  )
  # Photo effects (keys 1-9 on the open photo), spliced into tile.frag; listed by number
  # in src/gfx/tile_renderer.cpp.
  file(GLOB PB_PHOTO_SHADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/shaders/photo/*.frag")
  pb_embed_shaders(pb_gfx "${CMAKE_CURRENT_BINARY_DIR}/generated/photo_shaders.h" ${PB_PHOTO_SHADERS})

  # Plugins: optional, each built only when its dependencies are present.
  option(PB_BUILD_PLUGINS "Build the plugins whose dependencies are installed" ON)
  if(PB_BUILD_PLUGINS)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
    endif()
    if(FFMPEG_FOUND)
      pb_add_plugin(pb_video_plugin video plugins/video/video_plugin.cpp)
      target_link_libraries(pb_video_plugin PRIVATE PkgConfig::FFMPEG Threads::Threads)
      add_dependencies(photobrowse_plugins pb_video_plugin)
      add_dependencies(pb_tests pb_video_plugin)
      target_compile_definitions(pb_tests PRIVATE PB_TEST_VIDEO_PLUGIN)
    else()
      message(STATUS "FFmpeg development files not found: the video plugin will not be built.")
    endif()

    # Map tiles (M): HTTP(S) with libcurl, PNG/JPEG decoding with the vendored stb_image.
    find_package(CURL QUIET)
    if(CURL_FOUND)
      pb_add_plugin(pb_maptiles_plugin maptiles
        plugins/maptiles/maptiles_plugin.cpp
        plugins/maptiles/maptiles_util.cpp)
      target_include_directories(pb_maptiles_plugin PRIVATE "${CMAKE_SOURCE_DIR}/third_party/stb")
      target_compile_definitions(pb_maptiles_plugin PRIVATE PB_VERSION="${PROJECT_VERSION}")
      target_link_libraries(pb_maptiles_plugin PRIVATE CURL::libcurl Threads::Threads)
      add_dependencies(photobrowse_plugins pb_maptiles_plugin)
      add_dependencies(pb_tests pb_maptiles_plugin)
      target_sources(pb_tests PRIVATE tests/test_maptiles.cpp plugins/maptiles/maptiles_util.cpp)
      target_link_libraries(pb_tests PRIVATE CURL::libcurl)
      target_compile_definitions(pb_tests PRIVATE PB_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
    else()
      message(STATUS "libcurl development files not found: the maptiles plugin will not be built.")
    endif()
  endif()

  add_executable(photobrowse
    src/main.cpp
    src/app/app.cpp
    src/app/gpu_textures.cpp
    src/app/sdl_audio.cpp
  )
  target_link_libraries(photobrowse PRIVATE pb_app pb_gfx SDL3::SDL3)
  add_dependencies(photobrowse photobrowse_plugins)
endif()
