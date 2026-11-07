# SPDX-License-Identifier: LGPL-2.1-or-later

include_directories(
    SYSTEM
    ${nlohmann_json_INCLUDE_DIRS}
    ${QtNetwork_INCLUDE_DIRS}
)

set(Agentic_LIBS
    FreeCADApp
    ${QtNetwork_LIBRARIES}
)

set(Agentic_SRCS
    AgentConfig.cpp
    AgentConfig.h
    AppAgentic.cpp
    ChangeLog.cpp
    ChangeLog.h
    EngineSession.cpp
    EngineSession.h
    FileStore.cpp
    FileStore.h
    HttpServer.cpp
    HttpServer.h
    MessageLog.cpp
    MessageLog.h
    PreCompiled.h
    Project.cpp
    Project.h
    ProjectSession.h
    Protocol.cpp
    Protocol.h
    Runtime.cpp
    Runtime.h
    Schema.cpp
    Schema.h
    SessionManager.cpp
    SessionManager.h
    SessionObserver.h
    SessionTools.cpp
    SessionTools.h
    StderrLogger.cpp
    StderrLogger.h
    StdioServer.cpp
    StdioServer.h
    Tools.cpp
    Tools.h
)

add_library(Agentic SHARED ${Agentic_SRCS})

if(FREECAD_USE_PCH)
    target_precompile_headers(Agentic PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:"${CMAKE_CURRENT_LIST_DIR}/PreCompiled.h">
    )
endif(FREECAD_USE_PCH)

target_link_libraries(Agentic ${Agentic_LIBS})
target_compile_warn_error(Agentic)

set_bin_dir(Agentic Agentic)
set_python_prefix_suffix(Agentic)

install(TARGETS Agentic DESTINATION ${CMAKE_INSTALL_LIBDIR})
