# mpkit -- multiphysics solver. Sister of sikit / pdnkit / emikit.
#
# This first commit ships the foundation library only:
#   - Grid + Field wrappers on circuitcore::field
#   - Material + MaterialLibrary
#   - BoundaryCondition primitives
#   - Voxelizer that turns a Board into a per-voxel material-id grid
#
# Solvers (thermal, mechanical), couplings to pdnkit/sikit, the 3D
# viewer widget library, and the studio tab are each their own PR.

add_library(mpkit_mp STATIC
    mp/JouleCoupling.cpp
    mp/MaterialLibrary.cpp
    mp/SteadyHeat.cpp
    mp/Voxelizer.cpp
    mp/Grid.h
    mp/Material.h
    mp/MaterialLibrary.h
    mp/BoundaryCondition.h
    mp/JouleCoupling.h
    mp/SteadyHeat.h
    mp/Voxelizer.h
)
add_library(mpkit::mp ALIAS mpkit_mp)

target_include_directories(mpkit_mp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mpkit_mp PUBLIC
    circuitcore::field
    circuitcore::board
    pdnkit_pi
    Eigen3::Eigen
)

circuitcore_warnings(mpkit_mp)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
