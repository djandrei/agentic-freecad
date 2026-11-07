# SPDX-License-Identifier: LGPL-2.1-or-later

include_directories(
    SYSTEM
    ${nlohmann_json_INCLUDE_DIRS}
)

add_executable(Agentic_tests_run
    Protocol.cpp
    Schema.cpp
)

target_include_directories(
    Agentic_tests_run
        PUBLIC
            ${CMAKE_BINARY_DIR}
)
