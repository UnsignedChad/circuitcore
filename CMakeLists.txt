cmake_minimum_required(VERSION 3.20)
project(circuitcore LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC removed stdext::checked_array_iterator from <iterator> in very
# recent toolchains (VS 17.10+ and VS 18 Insiders). VTK 9.3.1 ships an
# old fmt copy inside ThirdParty/diy2 that still references the type;
# this define keeps the deprecated alias around so the bundled fmt
# parses cleanly. No effect on non-MSVC builds. Must be set before any
# FetchContent / add_subdirectory pulls in code that may include STL
# headers, so it lives right after project().
if(MSVC)
    add_compile_definitions(_HAS_DEPRECATED_STDEXT_ARR_ITERS=1)

    # VTK auto-generates per-module export macros that expand to
    # __declspec(dllimport) on consumers when VTK is not built as a
    # shared lib but VTK_STATIC_DEFINE flags arent set. Result: every
    # inline member of a VTKCOMMONCORE_EXPORT class needs to come from
    # a DLL that does not exist, and the link of any binary using VTK
    # fails with LNK2019. Tell VTK headers were a static build.
    add_compile_definitions(
        VTKCOMMONCORE_STATIC_DEFINE
        VTKCOMMONDATAMODEL_STATIC_DEFINE
        VTKCOMMONEXECUTIONMODEL_STATIC_DEFINE
        VTKCOMMONMATH_STATIC_DEFINE
        VTKCOMMONMISC_STATIC_DEFINE
        VTKCOMMONSYSTEM_STATIC_DEFINE
        VTKCOMMONTRANSFORMS_STATIC_DEFINE
        VTKCOMMONCOLOR_STATIC_DEFINE
        VTKCOMMONCOMPUTATIONALGEOMETRY_STATIC_DEFINE
        VTKFILTERSCORE_STATIC_DEFINE
        VTKFILTERSGENERAL_STATIC_DEFINE
        VTKFILTERSSOURCES_STATIC_DEFINE
        VTKFILTERSGEOMETRY_STATIC_DEFINE
        VTKFILTERSEXTRACTION_STATIC_DEFINE
        VTKFILTERSMODELING_STATIC_DEFINE
        VTKFILTERSHYBRID_STATIC_DEFINE
        VTKFILTERSHYPERTREE_STATIC_DEFINE
        VTKFILTERSSTATISTICS_STATIC_DEFINE
        VTKFILTERSTEXTURE_STATIC_DEFINE
        VTKFILTERSVERDICT_STATIC_DEFINE
        VTKIMAGINGCORE_STATIC_DEFINE
        VTKIMAGINGCOLOR_STATIC_DEFINE
        VTKIMAGINGSOURCES_STATIC_DEFINE
        VTKIMAGINGGENERAL_STATIC_DEFINE
        VTKIMAGINGHYBRID_STATIC_DEFINE
        VTKINTERACTIONSTYLE_STATIC_DEFINE
        VTKINTERACTIONWIDGETS_STATIC_DEFINE
        VTKIOCORE_STATIC_DEFINE
        VTKIOIMAGE_STATIC_DEFINE
        VTKIOXML_STATIC_DEFINE
        VTKIOXMLPARSER_STATIC_DEFINE
        VTKIOLEGACY_STATIC_DEFINE
        VTKIOGEOMETRY_STATIC_DEFINE
        VTKPARALLELCORE_STATIC_DEFINE
        VTKPARALLELDIY_STATIC_DEFINE
        VTKRENDERINGCORE_STATIC_DEFINE
        VTKRENDERINGOPENGL2_STATIC_DEFINE
        VTKRENDERINGUI_STATIC_DEFINE
        VTKRENDERINGFREETYPE_STATIC_DEFINE
        VTKRENDERINGANNOTATION_STATIC_DEFINE
        VTKRENDERINGCONTEXT2D_STATIC_DEFINE
        VTKRENDERINGCONTEXTOPENGL2_STATIC_DEFINE
        VTKRENDERINGHYPERTREEGRID_STATIC_DEFINE
        VTKRENDERINGIMAGE_STATIC_DEFINE
        VTKRENDERINGVOLUME_STATIC_DEFINE
        VTKRENDERINGVOLUMEOPENGL2_STATIC_DEFINE
        VTKRENDERINGLABEL_STATIC_DEFINE
        VTKRENDERINGSCENEGRAPH_STATIC_DEFINE
        VTKRENDERINGVTKJS_STATIC_DEFINE
        VTKGUISUPPORTQT_STATIC_DEFINE
    )
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(CIRCUITCORE_BUILD_MPKIT_WIDGETS
       "Build mpkit::widgets (pulls in VTK via FetchContent, adds ~20 min)"
       ON)

option(CIRCUITCORE_BUILD_TESTS "Build all test suites" ON)

# --- Shared dependencies ---
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(spdlog REQUIRED)
find_package(CLI11 REQUIRED)

# Cross-compiler warning preset. GCC/Clang get the full -Wall -Wextra
# -Wpedantic trio; MSVC gets /W4 (its /Wall is famously too noisy for
# real codebases). Call as: circuitcore_warnings(target_name).
function(circuitcore_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()


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
add_subdirectory(board)    # circuitcore::board    -- canonical model
add_subdirectory(sexpr)    # circuitcore::sexpr    -- generic utility
add_subdirectory(netlist)  # circuitcore::netlist  -- schematic-side data

# --- Layer 2: format adapters (each produces a board::Board) ---
add_subdirectory(formats/kicad)   # circuitcore::kicad -> board::Board
add_subdirectory(formats/gmsh)    # circuitcore::formats::gmsh -- voxel hex -> .msh
# add_subdirectory(formats/altium)  # future
# add_subdirectory(formats/ipc2581) # future

# --- Layer 2.5: shared Qt UI ---
add_subdirectory(field)     # circuitcore::field -- shared 3D Field/Grid types
add_subdirectory(ui)        # circuitcore::ui -- shared renderer modules

# --- Layer 3: analysis tools ---
add_subdirectory(pdnkit)
add_subdirectory(sikit)
add_subdirectory(emikit)
add_subdirectory(mpkit)
add_subdirectory(studio)   # circuitcore_studio -- unified browser-tab GUI
                            # (see sikit/MIGRATION.md)

# --- Test discovery for the core libs ---
if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(sexpr/tests)
    add_subdirectory(formats/kicad/tests)
endif()
