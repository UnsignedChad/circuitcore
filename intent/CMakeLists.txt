# circuitcore::intent -- design-intent checks comparing Netlist to Board.
# Lives in core because both pdnkit and sikit want to call it before
# running their analyses (garbage-in / garbage-out is cheaper to catch
# at this layer than to debug at the result).
add_library(circuitcore_intent STATIC
    src/Checks.cpp
)
add_library(circuitcore::intent ALIAS circuitcore_intent)

target_include_directories(circuitcore_intent PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(circuitcore_intent
    PUBLIC circuitcore::board circuitcore::netlist
)
target_compile_options(circuitcore_intent PRIVATE -Wall -Wextra -Wpedantic)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
