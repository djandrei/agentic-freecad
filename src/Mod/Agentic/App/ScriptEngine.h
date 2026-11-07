// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class EngineSession;
class MessageLog;
class Protocol;
class Runtime;

/// Runs agent Python inside FreeCAD, with everything the agent needs to know
/// about what happened: output, value, exception, timeout, the objects the
/// run created, changed and deleted, the documents saved afterwards and the
/// console messages. The run is one transaction on the active document; an
/// exception rolls it back.
class AgenticExport ScriptEngine
{
public:
    ScriptEngine(EngineSession& session, MessageLog& messages, Runtime& runtime);

    nlohmann::json run(
        const std::optional<std::string>& code,
        const std::optional<std::string>& file,
        double timeoutSeconds,
        const std::optional<std::string>& document
    );

    nlohmann::json reset();

private:
    EngineSession& session;
    MessageLog& messages;
    Runtime& runtime;
};

AgenticExport void registerScriptTools(Protocol& protocol, Runtime& runtime);

}  // namespace Agentic
