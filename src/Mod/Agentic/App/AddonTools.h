// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class EngineSession;
class Protocol;
class Runtime;

/// Registers the addon tools: addon_catalog, addon_installed, addon_install,
/// addon_remove, addon_files, addon_registry_list, addon_registry_add.
AgenticExport void registerAddonTools(Protocol& protocol, Runtime& runtime);

/// Loads the addons installed in a project into the session. Problems are
/// reported in the result and on the console, never thrown.
AgenticExport nlohmann::json loadProjectAddons(EngineSession& session);

}  // namespace Agentic
