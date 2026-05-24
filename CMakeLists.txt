cmake_minimum_required(VERSION 3.20)
project(circuitcore LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(CIRCUITCORE_BUILD_TESTS "Build all test suites" ON)

# --- Shared dependencies ---
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(Eigen3 3.4 REQUIRED NO_MODULE)
find_package(spdlog REQUIRED)

# CHOLMOD is optional -- falls back to Eigen::SimplicialLLT.
find_path(CHOLMOD_INCLUDE_DIR cholmod.h PATHS /usr/include/suitesparse)
find_library(CHOLMOD_LIBRARY cholmod)
if(CHOLMOD_INCLUDE_DIR AND CHOLMOD_LIBRARY)
    message(STATUS "Found CHOLMOD: ${CHOLMOD_LIBRARY}")
    set(CIRCUITCORE_HAVE_CHOLMOD ON)
else()
    message(STATUS "CHOLMOD not found; using Eigen::SimplicialLLT fallback")
    set(CIRCUITCORE_HAVE_CHOLMOD OFF)
endif()

if(CIRCUITCORE_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 REQUIRED)
    include(Catch)
endif()

# --- Layer 1: format-independent core ---
add_subdirectory(board)    # circuitcore::board   -- canonical model
add_subdirectory(sexpr)    # circuitcore::sexpr   -- generic utility

# --- Layer 2: format adapters (each produces a board::Board) ---
add_subdirectory(formats/kicad)   # circuitcore::kicad -> board::Board
# add_subdirectory(formats/altium)  # future
# add_subdirectory(formats/ipc2581) # future

# --- Layer 3: analysis tools ---
add_subdirectory(pdnkit)
# add_subdirectory(sikit)   # uncomment once sikit migrates in
                            # (see sikit/MIGRATION.md)

# --- Test discovery for the core libs ---
if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(sexpr/tests)
    add_subdirectory(formats/kicad/tests)
endif()
