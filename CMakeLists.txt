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

add_library(mattergraph-core
  engine/src/midi/clipspec.cpp
  engine/src/midi/smf.cpp
  engine/src/skin/soundskin.cpp
  engine/src/dsp/modal_voice.cpp
  engine/src/render/renderer.cpp
  engine/src/render/wav.cpp)
target_include_directories(mattergraph-core PUBLIC engine/include)
target_link_libraries(mattergraph-core PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(mattergraph-core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(mattergraph-render renderer/app/main.cpp)
target_link_libraries(mattergraph-render PRIVATE mattergraph-core
  nlohmann_json::nlohmann_json)
target_compile_options(mattergraph-render PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_executable(mg_tests
  tests/test_clipspec_timeline.cpp
  tests/test_smf_timeline.cpp
  tests/test_modal_render.cpp)
target_link_libraries(mg_tests PRIVATE mattergraph-core Catch2::Catch2WithMain)
target_compile_options(mg_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(mg_tests PRIVATE
  MG_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  MG_SKINS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/skins")
add_test(NAME unit COMMAND mg_tests)
