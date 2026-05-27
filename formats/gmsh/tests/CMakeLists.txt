add_executable(circuitcore_formats_gmsh_tests
    msh_writer_test.cpp
)
target_link_libraries(circuitcore_formats_gmsh_tests PRIVATE
    circuitcore::formats::gmsh
    Catch2::Catch2WithMain
)
circuitcore_warnings(circuitcore_formats_gmsh_tests)
catch_discover_tests(circuitcore_formats_gmsh_tests)
