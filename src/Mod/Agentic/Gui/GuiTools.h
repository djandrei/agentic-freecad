// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;

/// Registers the workbench, command, task panel and selection tools.
AgenticGuiExport void registerGuiTools(Protocol& protocol, Runtime& runtime);

}  // namespace Agentic
