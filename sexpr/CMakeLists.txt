# circuitcore::sexpr -- generic S-expression reader.
# Currently used by formats/kicad; future format adapters can use it too.
add_library(circuitcore_sexpr STATIC
    src/SExpr.cpp
)
add_library(circuitcore::sexpr ALIAS circuitcore_sexpr)

target_include_directories(circuitcore_sexpr PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
circuitcore_warnings(circuitcore_sexpr)
