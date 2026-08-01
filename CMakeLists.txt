cmake_minimum_required(VERSION 3.24)
project(mattergraph LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1)
FetchContent_MakeAvailable(nlohmann_json Catch2)

# Shared warning policy. GCC 13's -Wdangling-reference is disabled: it false-
# positives on functions returning references tied to their arguments.
add_library(mg-warnings INTERFACE)
target_compile_options(mg-warnings INTERFACE
  -Wall -Wextra -Wpedantic -Werror
  $<$<CXX_COMPILER_ID:GNU>:-Wno-dangling-reference>)

add_library(mattergraph-core
  engine/src/midi/clipspec.cpp
  engine/src/midi/smf.cpp
  engine/src/skin/soundskin.cpp
  engine/src/dsp/modal_voice.cpp
  engine/src/render/renderer.cpp
  engine/src/render/wav.cpp)
target_include_directories(mattergraph-core PUBLIC engine/include)
target_link_libraries(mattergraph-core PRIVATE nlohmann_json::nlohmann_json
  mg-warnings)

add_executable(mattergraph-render renderer/app/main.cpp)
target_link_libraries(mattergraph-render PRIVATE mattergraph-core
  nlohmann_json::nlohmann_json mg-warnings)

enable_testing()
add_executable(mg_tests
  tests/test_clipspec_timeline.cpp
  tests/test_smf_timeline.cpp
  tests/test_modal_render.cpp)
target_link_libraries(mg_tests PRIVATE mattergraph-core Catch2::Catch2WithMain
  mg-warnings)
target_compile_definitions(mg_tests PRIVATE
  MG_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  MG_SKINS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/skins")
add_test(NAME unit COMMAND mg_tests)
