// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;
class EngineSession;

/// The project the tool call addresses: `arguments.project` when given (it
/// must be open), otherwise the current project. Makes it current. Throws
/// when there is none.
AgenticExport EngineSession& sessionFor(Runtime& runtime, const nlohmann::json& arguments);

/// Registers the project, file, log and change tools.
AgenticExport void registerSessionTools(Protocol& protocol, Runtime& runtime);

}  // namespace Agentic
