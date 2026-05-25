add_executable(circuitcore_sexpr_tests
    sexpr_test.cpp
    sexpr_emit_test.cpp
)
target_link_libraries(circuitcore_sexpr_tests PRIVATE
    circuitcore::sexpr
    Catch2::Catch2WithMain
)
circuitcore_warnings(circuitcore_sexpr_tests)
catch_discover_tests(circuitcore_sexpr_tests)
