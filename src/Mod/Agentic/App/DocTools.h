// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;

/// Registers the documentation tools, resources and prompts: docs_search,
/// docs_api, docs_command, docs_page, docs_status, docs_update; the
/// agentic://docs/api/{name}, agentic://wiki/{page}, agentic://project/changes
/// and agentic://project/log resources; the freecad-object-model prompt.
AgenticExport void registerDocTools(Protocol& protocol, Runtime& runtime);

}  // namespace Agentic
