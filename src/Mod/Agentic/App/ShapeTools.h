// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;

/// Registers geometry_info, geometry_check, shape_elements, import, export and
/// file_formats. They need only the App layer.
AgenticExport void registerShapeTools(Protocol& protocol, Runtime& runtime);

}  // namespace Agentic
