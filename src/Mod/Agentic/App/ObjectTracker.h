// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <set>
#include <string>
#include <utility>

#include <fastsignals/connection.h>
#include <nlohmann/json.hpp>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

namespace Agentic
{

/// Records the objects created, changed and deleted while it lives, through
/// the application's object signals. Used around a script or command run.
struct ObjectTracker
{
    using Key = std::pair<std::string, std::string>;  // document, object

    std::set<Key> created;
    std::set<Key> changed;
    std::set<Key> deleted;
    fastsignals::scoped_connection newConn;
    fastsignals::scoped_connection changeConn;
    fastsignals::scoped_connection deleteConn;

    ObjectTracker()
    {
        App::Application& app = App::GetApplication();
        newConn = app.signalNewObject.connect([this](const App::DocumentObject& obj) {
            created.insert(key(obj));
        });
        changeConn = app.signalChangedObject.connect(
            [this](const App::DocumentObject& obj, const App::Property&) {
                // An object signals a last change while it is being removed.
                const Key k = key(obj);
                if (!created.contains(k) && !deleted.contains(k)) {
                    changed.insert(k);
                }
            }
        );
        deleteConn = app.signalDeletedObject.connect([this](const App::DocumentObject& obj) {
            const Key k = key(obj);
            if (created.erase(k) == 0) {
                changed.erase(k);
                deleted.insert(k);
            }
        });
    }

    static Key key(const App::DocumentObject& obj)
    {
        const App::Document* doc = obj.getDocument();
        const char* name = obj.getNameInDocument();
        return {doc ? doc->getName() : "", name ? name : ""};
    }

    static nlohmann::json list(const std::set<Key>& items)
    {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& [doc, name] : items) {
            out.push_back({{"document", doc}, {"object", name}});
        }
        return out;
    }

    nlohmann::json toJson() const
    {
        return {{"created", list(created)}, {"changed", list(changed)}, {"deleted", list(deleted)}};
    }
};

}  // namespace Agentic
