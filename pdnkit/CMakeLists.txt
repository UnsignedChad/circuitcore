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
    pi/Thermal.cpp
    pi/Roughness.cpp
    pi/Sensitivity.cpp
    pi/Mor.cpp
    StackupWriter.cpp
    render/IrResultMesh.cpp
)
target_include_directories(pdnkit_pi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(pdnkit_pi PUBLIC
    circuitcore::board
    circuitcore::ui      # shared Qt renderer (Camera, meshers)
    Eigen3::Eigen
    spdlog::spdlog
    circuitcore::sexpr
)
if(CIRCUITCORE_HAVE_CHOLMOD)
    target_compile_definitions(pdnkit_pi PUBLIC PDNKIT_HAVE_CHOLMOD)
    target_include_directories(pdnkit_pi PRIVATE ${CHOLMOD_INCLUDE_DIR})
    target_link_libraries(pdnkit_pi PUBLIC ${CHOLMOD_LIBRARY})
endif()
target_compile_options(pdnkit_pi PRIVATE -Wall -Wextra -Wpedantic)

# Embeddable Qt widgets. Separated from the GUI executable so that
# downstream apps (the studio shell) can link them without pulling in
# pdnkit/main.cpp + MainWindow.cpp.
add_library(pdnkit_widgets STATIC
    PcbCanvas.cpp
    PcbCanvas.h
    LayerPanel.cpp
    LayerPanel.h
    AnalysisPanel.cpp
    AnalysisPanel.h
    ColorLegend.cpp
    ColorLegend.h
    DrcPanel.cpp
    DrcPanel.h
    CavityPanel.cpp
    CavityPanel.h
    NetStatsPanel.cpp
    NetStatsPanel.h
    TransientPanel.cpp
    TransientPanel.h
    ZfPlotWidget.cpp
    ZfPlotWidget.h
    TransientPlotWidget.cpp
    TransientPlotWidget.h
)
add_library(pdnkit::widgets ALIAS pdnkit_widgets)
set_target_properties(pdnkit_widgets PROPERTIES AUTOMOC ON)
target_include_directories(pdnkit_widgets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}    # so studio can use "pdnkit/PcbCanvas.h" etc.
)
target_link_libraries(pdnkit_widgets PUBLIC
    circuitcore::ui
    pdnkit_pi
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
)
target_compile_options(pdnkit_widgets PRIVATE -Wall -Wextra -Wpedantic)

# GUI binary. Needs the KiCad adapter to open files, plus Qt.
add_executable(pdnkit
    main.cpp
    MainWindow.cpp
)
set_target_properties(pdnkit PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)
target_link_libraries(pdnkit PRIVATE
    pdnkit::widgets
    pdnkit_pi
    circuitcore::kicad
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
    CLI11::CLI11
)
target_compile_options(pdnkit PRIVATE -Wall -Wextra -Wpedantic)

if(CIRCUITCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
