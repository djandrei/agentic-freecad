// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "EngineSession.h"
#include "SessionManager.h"

namespace fs = std::filesystem;

namespace Agentic
{

SessionManager::SessionManager(ObserverList& observers, MessageLog& messages, Runtime& runtime)
    : observerList(observers)
    , messageLog(messages)
    , runtime(runtime)
{}

SessionManager::~SessionManager() = default;

EngineSession& SessionManager::open(const fs::path& root, bool create)
{
    fs::path folder = root;
    if (!fs::exists(folder)) {
        if (!create) {
            throw std::invalid_argument("no such folder: " + root.string());
        }
        fs::create_directories(folder);
    }
    if (!fs::is_directory(folder)) {
        throw std::invalid_argument("not a folder: " + root.string());
    }
    folder = fs::weakly_canonical(folder);

    for (const auto& session : sessions) {
        if (session->project().root() == folder) {
            setCurrent(session.get());
            return *session;
        }
    }

    auto session = std::make_unique<EngineSession>(folder, observerList, messageLog, runtime);
    EngineSession* raw = session.get();
    sessions.push_back(std::move(session));
    observerList.each([raw](SessionObserver& o) { o.projectOpened(raw->project()); });
    setCurrent(raw);
    return *raw;
}

EngineSession* SessionManager::find(const std::string& nameOrPath) const
{
    for (const auto& session : sessions) {
        if (session->project().settings().name == nameOrPath) {
            return session.get();
        }
    }
    std::error_code ec;
    const fs::path wanted = fs::weakly_canonical(fs::path(nameOrPath), ec);
    if (!ec) {
        for (const auto& session : sessions) {
            if (session->project().root() == wanted) {
                return session.get();
            }
        }
    }
    return nullptr;
}

std::vector<EngineSession*> SessionManager::list() const
{
    std::vector<EngineSession*> out;
    for (const auto& session : sessions) {
        out.push_back(session.get());
    }
    return out;
}

EngineSession* SessionManager::current() const
{
    return active;
}

void SessionManager::setCurrent(EngineSession* session)
{
    if (active == session) {
        return;
    }
    active = session;
    observerList.each([session](SessionObserver& o) {
        o.currentProjectChanged(session ? &session->project() : nullptr);
    });
}

ObserverList& SessionManager::observers()
{
    return observerList;
}

}  // namespace Agentic
