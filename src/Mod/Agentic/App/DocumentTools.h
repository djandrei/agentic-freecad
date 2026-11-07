// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;

/// Registers the document and object tools.
AgenticExport void registerDocumentTools(Protocol& protocol, Runtime& runtime);

}  // namespace Agentic
