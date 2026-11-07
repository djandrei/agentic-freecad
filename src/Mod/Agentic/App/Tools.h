// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;

/// Registers the tools that need nothing but the App layer: `version` and `status`.
AgenticExport void registerCoreTools(Protocol& protocol, const Runtime& runtime);

/// Registers the core resources: the server status as JSON.
AgenticExport void registerCoreResources(Protocol& protocol, const Runtime& runtime);

/// Registers the core prompts and the `initialize` instructions: the agent loop.
AgenticExport void registerCorePrompts(Protocol& protocol);

}  // namespace Agentic
