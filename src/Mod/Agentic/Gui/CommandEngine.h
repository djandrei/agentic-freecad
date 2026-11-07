// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Runtime;

/// Runs GUI commands the way a toolbar button does, and reports what they did.
///
/// The macro recorder captures the Python each command issues; the object
/// tracker records what changed; an opened task panel is described so the
/// agent can drive it; modal dialogs are answered by the DialogGuard and
/// listed. The run is logged as a `command` change with the recorded Python.
class AgenticGuiExport CommandEngine
{
public:
    explicit CommandEngine(Runtime& runtime);

    nlohmann::json run(const std::string& name);

    /// Whether the command can run now. Some commands (Std_Expressions among
    /// them) dereference their action in isActive(), so the action is created
    /// first, as the classic GUI has done for every command it shows. Throws
    /// for an unknown name.
    static bool isActive(const std::string& name);

private:
    Runtime& runtime;
};

}  // namespace Agentic
