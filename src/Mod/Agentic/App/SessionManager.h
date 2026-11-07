// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <Mod/Agentic/AgenticGlobal.h>

#include "SessionObserver.h"

namespace Agentic
{

class EngineSession;

/// One session per open project, and the current one.
///
/// The current project is the one the last tool call addressed; the viewer
/// follows it.
class AgenticExport SessionManager
{
public:
    explicit SessionManager(ObserverList& observers);
    ~SessionManager();

    /// Opens the project in the folder (creating the settings file when
    /// missing, or the folder itself when create is set) and makes it current.
    EngineSession& open(const std::filesystem::path& root, bool create);

    /// Finds an open project by its root path or its name; null when none.
    EngineSession* find(const std::string& nameOrPath) const;

    std::vector<EngineSession*> list() const;
    EngineSession* current() const;
    void setCurrent(EngineSession* session);

    ObserverList& observers();

private:
    ObserverList& observerList;
    std::vector<std::unique_ptr<EngineSession>> sessions;
    EngineSession* active {nullptr};
};

}  // namespace Agentic
