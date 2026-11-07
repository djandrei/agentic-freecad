// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;

/// Registers camera_get, camera_set, view_options, view_capture and drawing_export.
AgenticGuiExport void registerViewTools(Protocol& protocol, Runtime& runtime);

}  // namespace Agentic
