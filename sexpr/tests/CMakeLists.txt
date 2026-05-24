add_executable(circuitcore_sexpr_tests
    sexpr_test.cpp
)
target_link_libraries(circuitcore_sexpr_tests PRIVATE
    circuitcore::sexpr
    Catch2::Catch2WithMain
)
target_compile_options(circuitcore_sexpr_tests PRIVATE
    -Wall -Wextra -Wpedantic
)
catch_discover_tests(circuitcore_sexpr_tests)
