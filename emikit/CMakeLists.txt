# emikit -- radiated emissions pre-compliance tool.
# Math library + a CLI binary. No Qt in v1.

add_library(emikit_emi STATIC
    emi/Spectrum.cpp
    emi/LoopEmissions.cpp
    emi/CableCommonMode.cpp
    emi/Masks.cpp
    emi/BoardAnalysis.cpp
)
target_include_directories(emikit_emi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(emikit_emi PUBLIC circuitcore::board)
target_compile_options(emikit_emi PRIVATE -Wall -Wextra -Wpedantic)

add_executable(emikit
    main.cpp
)
target_link_libraries(emikit PRIVATE
    emikit_emi
    circuitcore::kicad
    CLI11::CLI11
)
target_compile_options(emikit PRIVATE -Wall -Wextra -Wpedantic)

add_executable(emikit_validate_ti
    tools/validate_ti.cpp
)
target_link_libraries(emikit_validate_ti PRIVATE emikit_emi)
target_compile_options(emikit_validate_ti PRIVATE -Wall -Wextra -Wpedantic)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
