// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

/// Calls `module.function(jsonString)` in the embedded interpreter and parses
/// the JSON string it returns. A Python exception becomes a std::runtime_error
/// carrying the message and the traceback. Takes the GIL.
AgenticExport nlohmann::json callPython(
    const char* module,
    const char* function,
    const nlohmann::json& argument
);

}  // namespace Agentic
