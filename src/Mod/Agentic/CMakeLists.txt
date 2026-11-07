# SPDX-License-Identifier: LGPL-2.1-or-later

add_subdirectory(App)

set(Agentic_Scripts
    Init.py
    TestAgenticApp.py
    agentic_addons.py
    agentic_docs.py
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

# The Python API index, generated from the .pyi binding stubs so it always
# matches this build. Configure-time glob: a new stub needs a re-configure,
# as it does for the binding generator.
file(GLOB_RECURSE Agentic_API_STUBS
    LIST_DIRECTORIES false
    ${CMAKE_SOURCE_DIR}/src/App/*.pyi
    ${CMAKE_SOURCE_DIR}/src/Base/*.pyi
    ${CMAKE_SOURCE_DIR}/src/Gui/*.pyi
    ${CMAKE_SOURCE_DIR}/src/Mod/*.pyi
)
set(Agentic_API_INDEX ${CMAKE_BINARY_DIR}/Mod/Agentic/api_index.json)
add_custom_command(
    OUTPUT ${Agentic_API_INDEX}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_api_index.py
            ${CMAKE_SOURCE_DIR} ${Agentic_API_INDEX}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_api_index.py ${Agentic_API_STUBS}
    COMMENT "Building the Agentic Python API index from the binding stubs"
)
add_custom_target(AgenticApiIndex ALL DEPENDS ${Agentic_API_INDEX})
install(FILES ${Agentic_API_INDEX} DESTINATION Mod/Agentic)
