# circuitcore::formats::gmsh -- writer that emits ASCII gmsh v2.2 .msh
# files from mpkit's voxel hex grid. Lets us hand the same geometry to
# ElmerFEM for an independent solver cross-check, and gives users an
# escape hatch if they prefer Elmer / their own gmsh-aware tool over
# mpkit's built-in solvers.
add_library(circuitcore_formats_gmsh STATIC
    src/MshWriter.cpp
    src/GeoWriter.cpp
)
add_library(circuitcore::formats::gmsh ALIAS circuitcore_formats_gmsh)

target_include_directories(circuitcore_formats_gmsh PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(circuitcore_formats_gmsh
    PUBLIC mpkit::mp circuitcore::board
)
circuitcore_warnings(circuitcore_formats_gmsh)

# Experimental: pcb2geo CLI -- KiCad .kicad_pcb -> Gmsh .geo so the
# conformal-mesh-export branch can be exercised end-to-end via
#   ./pcb2geo board.kicad_pcb -o board.geo && gmsh -3 board.geo
add_executable(pcb2geo src/pcb2geo_main.cpp)
target_link_libraries(pcb2geo PRIVATE
    circuitcore::formats::gmsh
    circuitcore::kicad
    circuitcore::board
)
circuitcore_warnings(pcb2geo)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
