cmake_minimum_required(VERSION 3.24)
# The build number shown on the main menu and in the window title. Bump it for every build handed out
# (the zip is named Crisp-v<version>.zip).
project(Crisp VERSION 0.23.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Static MSVC runtime: the .exe runs without installing any Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CRISP_BUILD_GAME "Build the game executable (needs SDL3)" ON)

# --- Simulation core: no SDL, no OpenGL. Shared by the game and the headless tests. ---
add_library(crisp_sim STATIC
  src/world.cpp
  src/movement.cpp
  src/combat.cpp
  src/items.cpp
  src/stats.cpp
  src/replay.cpp
  src/nav.cpp
  src/bots.cpp)
target_include_directories(crisp_sim PUBLIC src)
if(MSVC)
  # /fp:precise (default) - never /fp:fast: movement must be deterministic.
  target_compile_options(crisp_sim PUBLIC /W4 /permissive- /fp:precise)
else()
  target_compile_options(crisp_sim PUBLIC -Wall -Wextra -pedantic -ffp-contract=off)
endif()

enable_testing()
add_executable(sim_tests tests/sim_tests.cpp)
target_link_libraries(sim_tests PRIVATE crisp_sim)
add_test(NAME sim_tests COMMAND sim_tests)

if(CRISP_BUILD_GAME)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(SDL3)
  # ENet (MIT): small, reliable/unreliable UDP for online play.
  FetchContent_Declare(enet
    URL https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz
    URL_HASH SHA256=28603c895f9ed24a846478180ee72c7376b39b4bb1287b73877e5eae7d96b0dd
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(enet)
  # miniupnpc (BSD): asks the home router to open the host's port.
  set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
  set(UPNPC_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(miniupnpc
    URL https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_2_8.tar.gz
    URL_HASH SHA256=3d0d49f761f09c1321ceb663d1b008825361d7bd242640c60ae467e82642502a
    SOURCE_SUBDIR miniupnpc
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(miniupnpc)
  foreach(lib enet libminiupnpc-static)
    if(MSVC)
      target_compile_options(${lib} PRIVATE /w)
    else()
      target_compile_options(${lib} PRIVATE -w)
    endif()
  endforeach()

  # Third-party single-file decoders (public domain / MIT): Ogg Vorbis sounds and JPEG textures from
  # assets/. Built on their own, without our warning flags.
  add_library(crisp_third_party STATIC third_party/stb_vorbis.c third_party/stb_image_impl.c third_party/stb_truetype_impl.c)
  target_include_directories(crisp_third_party PUBLIC third_party)
  if(MSVC)
    target_compile_options(crisp_third_party PRIVATE /w)
  else()
    target_compile_options(crisp_third_party PRIVATE -w)
  endif()

  add_executable(crisp WIN32
    src/main.cpp
    src/render.cpp
    src/gl.cpp
    src/config.cpp
    src/audio.cpp
    src/fx.cpp
    src/net.cpp
    src/upnp.cpp)
  target_link_libraries(crisp PRIVATE crisp_sim crisp_third_party enet libminiupnpc-static SDL3::SDL3-static)
  target_include_directories(crisp PRIVATE ${enet_SOURCE_DIR}/include)
  if(WIN32)
    target_link_libraries(crisp PRIVATE ws2_32 winmm)  # ENet's sockets and timer
  endif()
  target_compile_definitions(crisp PRIVATE CRISP_VERSION="${PROJECT_VERSION}")
  if(MSVC)
    target_link_options(crisp PRIVATE /INCREMENTAL:NO)
  endif()
  # Copy the player README next to the executable (config.cfg is written on first launch).
  # assets/ is replaced, not merged: a stale sound variant left behind would still be played.
  add_custom_command(TARGET crisp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/README.md $<TARGET_FILE_DIR:crisp>/README.md
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:crisp>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:crisp>/assets)
endif()
