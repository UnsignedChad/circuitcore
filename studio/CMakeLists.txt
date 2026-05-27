# circuitcore_studio -- unified browser-tab shell for every kit. One
# window, five tabs (Board, SI, PI, EMI, Mp), one shared BoardModel.
# Each tab brings its own toolbar + panels; the model carries the
# loaded PCB + selection state and broadcasts via signals.
#
# Depends on:
#   - circuitcore::ui      (shared 2D renderer; LayerColors here)
#   - circuitcore::kicad   (PcbParser for File > Open)
#   - sikit_si             (SI workflows + SiStackup paired with the board)
#   - pdnkit_widgets       (PI panels + canvas + IR/DRC dialogs)
#   - emikit               (EMI drive + spectrum)
#   - mpkit_mp + mpkit_widgets (multiphysics + 3D VTK viewer)
#   - Qt6::Widgets

add_executable(circuitcore_studio
    main.cpp
    StudioWindow.cpp
    BoardModel.cpp
    BoardTab.cpp
    SiTab.cpp
    PiTab.cpp
    EmiTab.cpp
    MpTab.cpp
    CalculatorsDialog.cpp
)

target_include_directories(circuitcore_studio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(circuitcore_studio PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
)

target_link_libraries(circuitcore_studio PRIVATE
    circuitcore::ui
    circuitcore::board
    circuitcore::kicad
    sikit_si              # SiStackup parsing + channel synthesis
    sikit::widgets        # SParamPlotWindow + EyeWindow
    pdnkit::widgets       # PcbCanvas + AnalysisPanel + NetStatsPanel + ...
    pdnkit_pi             # IrMesher + IrSolver + Thermal for PiTab slots
    mpkit::mp             # voxelizer / steady heat / joule for MpTab demo
    emikit_emi            # analyze_board for the EMI tab
    Qt6::Widgets
)

if(CIRCUITCORE_BUILD_MPKIT_WIDGETS)
    target_link_libraries(circuitcore_studio PRIVATE mpkit::widgets)
    target_compile_definitions(circuitcore_studio PRIVATE MPKIT_HAS_WIDGETS)
endif()

circuitcore_warnings(circuitcore_studio)
