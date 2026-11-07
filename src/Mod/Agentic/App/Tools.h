// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;

/// Registers the tools that need nothing but the App layer: `version` and `status`.
AgenticExport void registerCoreTools(Protocol& protocol, const Runtime& runtime);

}  // namespace Agentic
