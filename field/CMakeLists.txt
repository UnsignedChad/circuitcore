# circuitcore::field -- shared 3D scalar/vector field + Cartesian grid
# math used by every C++ kit that does volumetric simulation.
#
# Started life as sikit::fdtd::Field3D + GridSpec; mpkit wanted the same
# types for its thermal / mechanical / coupled solvers, so they moved
# here. sikit::fdtd retains using-declarations for source-compat.
#
# Header-only types plus one .cpp for cfl_dt. Linked PUBLIC so the EM
# constants propagate.

add_library(circuitcore_field STATIC
    src/Field3D.cpp
    include/circuitcore/field/Field3D.h
    include/circuitcore/field/GridSpec.h
)
add_library(circuitcore::field ALIAS circuitcore_field)

target_include_directories(circuitcore_field PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

circuitcore_warnings(circuitcore_field)
