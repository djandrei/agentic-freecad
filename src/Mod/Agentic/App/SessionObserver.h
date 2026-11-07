// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Project;
struct WriteResult;
struct ChangeEntry;
struct MessageEntry;

/// What the viewer (or any other watcher) can be told about a session.
///
/// Every callback runs on the main thread, inside the tool call that caused
/// the event. Default implementations do nothing, so a watcher overrides what
/// it needs.
class AgenticExport SessionObserver
{
public:
    virtual ~SessionObserver() = default;

    virtual void projectOpened(const Project& /*project*/)
    {}
    virtual void currentProjectChanged(const Project* /*project*/)
    {}
    virtual void projectSettingsChanged(const Project& /*project*/)
    {}
    virtual void fileWritten(const Project& /*project*/, const WriteResult& /*result*/)
    {}
    virtual void fileRemoved(const Project& /*project*/, const std::string& /*path*/)
    {}
    virtual void fileRenamed(
        const Project& /*project*/,
        const std::string& /*from*/,
        const std::string& /*to*/
    )
    {}
    virtual void changeLogged(const Project& /*project*/, const ChangeEntry& /*entry*/)
    {}
    /// event: opened, created, saved, closed, recomputed, undo, redo, objects
    virtual void documentChanged(
        const Project& /*project*/,
        const std::string& /*document*/,
        const std::string& /*event*/
    )
    {}
    virtual void toolCalled(
        const std::string& /*name*/,
        const nlohmann::json& /*arguments*/,
        double /*milliseconds*/,
        bool /*ok*/
    )
    {}
};

/// The observers of the runtime; events are delivered to each in turn.
class AgenticExport ObserverList
{
public:
    void add(SessionObserver* observer)
    {
        if (std::find(observers.begin(), observers.end(), observer) == observers.end()) {
            observers.push_back(observer);
        }
    }
    void remove(SessionObserver* observer)
    {
        observers.erase(std::remove(observers.begin(), observers.end(), observer), observers.end());
    }
    template<typename Fn>
    void each(Fn&& fn) const
    {
        // Copy: an observer may remove itself while being notified.
        const std::vector<SessionObserver*> snapshot = observers;
        for (SessionObserver* observer : snapshot) {
            fn(*observer);
        }
    }

private:
    std::vector<SessionObserver*> observers;
};

}  // namespace Agentic
