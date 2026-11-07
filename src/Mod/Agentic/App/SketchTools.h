// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;
class Runtime;

/// Registers sketch_get, sketch_add, sketch_constrain, sketch_remove and
/// sketch_solve over the Sketcher API. They need only the App layer.
AgenticExport void registerSketchTools(Protocol& protocol, Runtime& runtime);

}  // namespace Agentic
