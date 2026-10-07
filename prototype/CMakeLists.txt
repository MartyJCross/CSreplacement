cmake_minimum_required(VERSION 3.24)
project(FeelLab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Static MSVC runtime: the .exe runs without installing any Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(FEELLAB_BUILD_GAME "Build the game executable (needs SDL3)" ON)

# --- Simulation core: no SDL, no OpenGL. Shared by the game and the headless tests. ---
add_library(feellab_sim STATIC
  src/world.cpp
  src/movement.cpp
  src/combat.cpp)
target_include_directories(feellab_sim PUBLIC src)
if(MSVC)
  # /fp:precise (default) - never /fp:fast: movement must be deterministic.
  target_compile_options(feellab_sim PUBLIC /W4 /permissive- /fp:precise)
else()
  target_compile_options(feellab_sim PUBLIC -Wall -Wextra -pedantic -ffp-contract=off)
endif()

enable_testing()
add_executable(sim_tests tests/sim_tests.cpp)
target_link_libraries(sim_tests PRIVATE feellab_sim)
add_test(NAME sim_tests COMMAND sim_tests)

if(FEELLAB_BUILD_GAME)
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

  add_executable(feellab WIN32
    src/main.cpp
    src/render.cpp
    src/gl.cpp
    src/config.cpp
    src/audio.cpp
    src/fx.cpp)
  target_link_libraries(feellab PRIVATE feellab_sim SDL3::SDL3-static)
  if(MSVC)
    target_link_options(feellab PRIVATE /INCREMENTAL:NO)
  endif()
  # Copy the player README next to the executable (config.cfg is written on first launch).
  add_custom_command(TARGET feellab POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/README.md $<TARGET_FILE_DIR:feellab>/README.md)
endif()
