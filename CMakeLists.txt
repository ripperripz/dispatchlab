cmake_minimum_required(VERSION 3.20)
project(DispatchLab VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(DISPATCHLAB_WERROR "Treat warnings in project code as errors" ON)
option(DISPATCHLAB_WITH_OSM "Build the libosmium-based OSM importer" ON)

# Warnings apply to project targets only; third-party headers are SYSTEM includes.
set(DL_WARNINGS -Wall -Wextra -Wpedantic -Wshadow)
if(DISPATCHLAB_WERROR)
  list(APPEND DL_WARNINGS -Werror)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------- core library
add_library(dispatchlab_core STATIC
  src/graph/csr_graph.cpp
  src/graph/scc.cpp
  src/routing/dijkstra.cpp
  src/routing/bidirectional.cpp
  src/routing/astar.cpp
  src/simulation/event_queue.cpp
  src/simulation/scenario.cpp
  src/simulation/traffic.cpp
  src/simulation/simulator.cpp
  src/simulation/replay.cpp
  src/simulation/event_log.cpp
  src/dispatch/nearest.cpp
  src/dispatch/greedy_eta.cpp
  src/util/json.cpp
  src/util/sysinfo.cpp
  src/experiments/experiment.cpp
)
target_include_directories(dispatchlab_core PUBLIC include)
target_compile_options(dispatchlab_core PRIVATE ${DL_WARNINGS})
target_link_libraries(dispatchlab_core PUBLIC Threads::Threads)
target_compile_definitions(dispatchlab_core PRIVATE
  DL_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
  DL_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
  DL_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  DL_CXX_FLAGS="${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")

# ---------------------------------------------------------------- OSM importer (libosmium)
if(DISPATCHLAB_WITH_OSM)
  find_path(OSMIUM_INCLUDE_DIR osmium/version.hpp
    HINTS ENV OSMIUM_ROOT PATHS /opt/homebrew/include /usr/local/include /usr/include)
  find_path(PROTOZERO_INCLUDE_DIR protozero/version.hpp
    HINTS ENV PROTOZERO_ROOT PATHS /opt/homebrew/include /usr/local/include /usr/include)
  find_package(ZLIB REQUIRED)
  find_package(BZip2 REQUIRED)
  find_package(EXPAT REQUIRED)
  if(NOT OSMIUM_INCLUDE_DIR OR NOT PROTOZERO_INCLUDE_DIR)
    message(FATAL_ERROR "libosmium/protozero headers not found. Install libosmium-dev "
                        "(apt) or libosmium (brew), or configure with -DDISPATCHLAB_WITH_OSM=OFF.")
  endif()
  add_library(dispatchlab_osm STATIC src/graph/osm_importer.cpp)
  target_include_directories(dispatchlab_osm SYSTEM PRIVATE ${OSMIUM_INCLUDE_DIR} ${PROTOZERO_INCLUDE_DIR})
  target_compile_options(dispatchlab_osm PRIVATE ${DL_WARNINGS})
  target_link_libraries(dispatchlab_osm PUBLIC dispatchlab_core PRIVATE ZLIB::ZLIB BZip2::BZip2 EXPAT::EXPAT)
endif()

# ---------------------------------------------------------------- CLI
add_executable(dispatchlab src/main.cpp)
target_compile_options(dispatchlab PRIVATE ${DL_WARNINGS})
if(DISPATCHLAB_WITH_OSM)
  target_link_libraries(dispatchlab PRIVATE dispatchlab_osm)
  target_compile_definitions(dispatchlab PRIVATE DL_WITH_OSM=1)
else()
  target_link_libraries(dispatchlab PRIVATE dispatchlab_core)
endif()

# ---------------------------------------------------------------- tests
enable_testing()
add_library(doctest_main STATIC tests/doctest_main.cpp)
target_include_directories(doctest_main SYSTEM PUBLIC third_party/doctest)

set(DL_TEST_SOURCES
  tests/test_heap.cpp
  tests/test_graph.cpp
  tests/test_scc.cpp
  tests/test_routing.cpp
  tests/test_simulator.cpp
  tests/test_dispatch.cpp
  tests/test_replay.cpp
)
if(DISPATCHLAB_WITH_OSM)
  list(APPEND DL_TEST_SOURCES tests/test_osm_import.cpp)
endif()
add_executable(dispatchlab_tests ${DL_TEST_SOURCES})
target_compile_options(dispatchlab_tests PRIVATE ${DL_WARNINGS})
target_link_libraries(dispatchlab_tests PRIVATE doctest_main)
target_compile_definitions(dispatchlab_tests PRIVATE
  DL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
if(DISPATCHLAB_WITH_OSM)
  target_link_libraries(dispatchlab_tests PRIVATE dispatchlab_osm)
else()
  target_link_libraries(dispatchlab_tests PRIVATE dispatchlab_core)
endif()
add_test(NAME unit_tests COMMAND dispatchlab_tests)

# Real-data tests: import the committed Tricity extract, then run the
# 10,000-query routing agreement suite on it.
add_executable(routing_agreement tests/routing_agreement.cpp)
target_compile_options(routing_agreement PRIVATE ${DL_WARNINGS})
target_link_libraries(routing_agreement PRIVATE dispatchlab_core)

set(DL_TRICITY_PBF "${CMAKE_CURRENT_SOURCE_DIR}/data/raw/tricity-261017.osm.pbf")
set(DL_TRICITY_GRAPH "${CMAKE_CURRENT_BINARY_DIR}/tricity.graph")
if(DISPATCHLAB_WITH_OSM)
  add_test(NAME import_tricity COMMAND dispatchlab import ${DL_TRICITY_PBF} ${DL_TRICITY_GRAPH} --clip-tricity)
  set_tests_properties(import_tricity PROPERTIES FIXTURES_SETUP tricity_graph)
  add_test(NAME routing_agreement_tricity
           COMMAND routing_agreement ${DL_TRICITY_GRAPH} --queries 10000 --seed 20261018)
  set_tests_properties(routing_agreement_tricity PROPERTIES FIXTURES_REQUIRED tricity_graph)
endif()

# ---------------------------------------------------------------- benchmarks
foreach(bench benchmark_graph benchmark_routing benchmark_dispatch)
  add_executable(${bench} benchmarks/${bench}.cpp)
  target_compile_options(${bench} PRIVATE ${DL_WARNINGS})
  target_link_libraries(${bench} PRIVATE dispatchlab_core)
endforeach()
