# mpkit foundation tests.

add_executable(mpkit_tests
    grid_test.cpp
    material_test.cpp
    steady_heat_test.cpp
    transient_heat_test.cpp
    voxelizer_test.cpp
)

target_link_libraries(mpkit_tests PRIVATE
    mpkit::mp
    Catch2::Catch2WithMain
)

circuitcore_warnings(mpkit_tests)

catch_discover_tests(mpkit_tests)
