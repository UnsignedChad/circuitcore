# pdnkit -- Power Integrity tool. Builds pdnkit_pi static lib + pdnkit
# GUI executable + tests.

# PI math + render -- no Qt deps. Uses the canonical board model but does
# not parse files itself, so only links circuitcore::board.
add_library(pdnkit_pi STATIC
    pi/IrMesher.cpp
    pi/IrSolver.cpp
    pi/CavityModel.cpp
    pi/DecapOptimizer.cpp
    pi/Transient.cpp
    pi/PowerDrc.cpp
    pi/SpiceExport.cpp
    pi/TargetZ.cpp
    pi/ViaInductance.cpp
    pi/Dielectric.cpp
    pi/Touchstone.cpp
    pi/Vrm.cpp
    render/Camera2D.cpp
    render/ZoneMesher.cpp
    render/IrResultMesh.cpp
    render/SegmentMesher.cpp
    render/ViaMesher.cpp
)
target_include_directories(pdnkit_pi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(pdnkit_pi PUBLIC
    circuitcore::board
    Eigen3::Eigen
    spdlog::spdlog
)
if(CIRCUITCORE_HAVE_CHOLMOD)
    target_compile_definitions(pdnkit_pi PUBLIC PDNKIT_HAVE_CHOLMOD)
    target_include_directories(pdnkit_pi PRIVATE ${CHOLMOD_INCLUDE_DIR})
    target_link_libraries(pdnkit_pi PUBLIC ${CHOLMOD_LIBRARY})
endif()
target_compile_options(pdnkit_pi PRIVATE -Wall -Wextra -Wpedantic)

# GUI binary. Needs the KiCad adapter to open files, plus Qt.
add_executable(pdnkit
    main.cpp
    MainWindow.cpp
    PcbCanvas.cpp
    LayerPanel.cpp
    AnalysisPanel.cpp
    ColorLegend.cpp
    CavityPanel.cpp
    NetStatsPanel.cpp
    TransientPanel.cpp
    ZfPlotWidget.cpp
    TransientPlotWidget.cpp
)
set_target_properties(pdnkit PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)
target_link_libraries(pdnkit PRIVATE
    pdnkit_pi
    circuitcore::kicad
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
)
target_compile_options(pdnkit PRIVATE -Wall -Wextra -Wpedantic)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
