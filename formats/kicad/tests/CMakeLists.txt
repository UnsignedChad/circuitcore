add_executable(circuitcore_kicad_tests
    parser_test.cpp
    netlist_parser_test.cpp
)
target_link_libraries(circuitcore_kicad_tests PRIVATE
    circuitcore::kicad
    Catch2::Catch2WithMain
)
target_compile_options(circuitcore_kicad_tests PRIVATE
    -Wall -Wextra -Wpedantic
)
catch_discover_tests(circuitcore_kicad_tests)
