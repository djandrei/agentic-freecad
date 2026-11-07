// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;

/// Registers preferences_get and preferences_set over the parameter tree.
AgenticExport void registerPreferenceTools(Protocol& protocol, Runtime& runtime);

}  // namespace Agentic
