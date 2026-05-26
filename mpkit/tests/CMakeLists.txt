# mpkit foundation tests.

add_executable(mpkit_tests
    elasticity_test.cpp
    grid_test.cpp
    material_test.cpp
    joule_coupling_test.cpp
    steady_heat_test.cpp
    study_test.cpp
    transient_heat_test.cpp
    voxelizer_test.cpp
)

target_link_libraries(mpkit_tests PRIVATE
    mpkit::mp
    Catch2::Catch2WithMain
)

circuitcore_warnings(mpkit_tests)

catch_discover_tests(mpkit_tests)
