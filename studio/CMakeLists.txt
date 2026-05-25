# circuitcore_studio -- unified browser-tab shell for sikit / pdnkit /
# emikit. One window, four tabs (Board, SI, PI, EMI), one shared
# BoardModel. Each tab brings its own toolbar + panels; the model
# carries the loaded PCB + selection state and broadcasts via signals.
#
# This skeleton has the shell + the Board tab. The SI/PI/EMI tabs are
# placeholder widgets until Tasks #3 / #4 / #5 wire in the actual
# analysis widgets from each tool.
#
# Depends on:
#   - circuitcore::ui      (shared 2D renderer; LayerColors here)
#   - circuitcore::kicad   (PcbParser for File > Open)
#   - sikit_si             (SiStackup to pair with the loaded board)
#   - Qt6::Widgets

add_executable(circuitcore_studio
    main.cpp
    StudioWindow.cpp
    BoardModel.cpp
    BoardTab.cpp
    SiTab.cpp
    PiTab.cpp
    EmiTab.cpp
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
    pdnkit_pi             # IrMesher + IrSolver for the PI tab
    emikit_emi            # analyze_board for the EMI tab
    Qt6::Widgets
)

target_compile_options(circuitcore_studio PRIVATE -Wall -Wextra -Wpedantic)
