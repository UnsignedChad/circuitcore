add_executable(circuitcore_kicad_tests
    parser_test.cpp
    netlist_parser_test.cpp
)
target_link_libraries(circuitcore_kicad_tests PRIVATE
    circuitcore::kicad
    Catch2::Catch2WithMain
)
circuitcore_warnings(circuitcore_kicad_tests)
catch_discover_tests(circuitcore_kicad_tests)
