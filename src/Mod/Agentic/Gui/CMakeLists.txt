# SPDX-License-Identifier: LGPL-2.1-or-later

set(AgenticGui_LIBS
    Agentic
    FreeCADGui
)

set(AgenticGui_SRCS
    AppAgenticGui.cpp
    PreCompiled.h
    ViewerShell.cpp
    ViewerShell.h
)

add_library(AgenticGui SHARED ${AgenticGui_SRCS})

if(FREECAD_USE_PCH)
    target_precompile_headers(AgenticGui PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:"${CMAKE_CURRENT_LIST_DIR}/PreCompiled.h">
    )
endif(FREECAD_USE_PCH)

target_link_libraries(AgenticGui ${AgenticGui_LIBS})
target_compile_warn_error(AgenticGui)

set_bin_dir(AgenticGui AgenticGui)
set_python_prefix_suffix(AgenticGui)

install(TARGETS AgenticGui DESTINATION ${CMAKE_INSTALL_LIBDIR})
