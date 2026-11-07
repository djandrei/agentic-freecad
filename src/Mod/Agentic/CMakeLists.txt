# SPDX-License-Identifier: LGPL-2.1-or-later

add_subdirectory(App)

set(Agentic_Scripts
    Init.py
    TestAgenticApp.py
    agentic_addons.py
    agentic_props.py
    agentic_script.py
    agentic_shapes.py
    agentic_sketch.py
    tools_reference.py
)

if(BUILD_GUI)
    add_subdirectory(Gui)
    list(APPEND Agentic_Scripts InitGui.py TestAgenticGui.py agentic_gui.py agentic_view.py)
endif(BUILD_GUI)

add_custom_target(AgenticScripts ALL
    SOURCES ${Agentic_Scripts}
)

fc_target_copy_resource(AgenticScripts
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/Mod/Agentic
    ${Agentic_Scripts})

install(
    FILES
        ${Agentic_Scripts}
    DESTINATION
        Mod/Agentic
)
