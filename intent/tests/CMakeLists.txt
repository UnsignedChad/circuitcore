add_executable(circuitcore_intent_tests
    intent_test.cpp
)
target_link_libraries(circuitcore_intent_tests PRIVATE
    circuitcore::intent
    Catch2::Catch2WithMain
)
circuitcore_warnings(circuitcore_intent_tests)
catch_discover_tests(circuitcore_intent_tests)
