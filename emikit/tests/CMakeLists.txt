add_executable(emikit_tests
    spectrum_test.cpp
    loop_test.cpp
    masks_test.cpp
    analysis_test.cpp
    calibration_test.cpp
    cable_cm_test.cpp
)
target_link_libraries(emikit_tests PRIVATE
    emikit_emi
    Catch2::Catch2WithMain
)
circuitcore_warnings(emikit_tests)
catch_discover_tests(emikit_tests)
