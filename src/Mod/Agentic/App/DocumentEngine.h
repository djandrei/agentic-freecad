// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Agentic
{

class EngineSession;
class MessageLog;

/// Documents, objects and properties of one project.
///
/// Every mutation is one transaction named after the tool, so document_undo
/// reverses exactly one call; the project's autoRecompute and autoSave
/// settings are applied afterwards and the console messages of the call are
/// returned with the result. Property values cross to and from JSON through
/// the Python helper `agentic_props`.
class AgenticExport DocumentEngine
{
public:
    DocumentEngine(EngineSession& session, MessageLog& messages);

    // documents
    nlohmann::json listDocuments() const;
    nlohmann::json openDocument(const std::string& file);
    nlohmann::json newDocument(const std::string& name, const std::optional<std::string>& file);
    nlohmann::json saveDocument(const std::string& document, const std::optional<std::string>& as);
    nlohmann::json closeDocument(const std::string& document);
    nlohmann::json recompute(const std::string& document, bool force);
    nlohmann::json undo(const std::string& document);
    nlohmann::json redo(const std::string& document);

    // objects
    nlohmann::json listObjects(const std::string& document, const std::optional<std::string>& type) const;
    nlohmann::json getObject(
        const std::string& document,
        const std::string& object,
        const nlohmann::json& properties,
        bool includeView
    ) const;
    nlohmann::json setObject(
        const std::string& document,
        const std::string& object,
        const nlohmann::json& properties,
        const nlohmann::json& view
    );
    nlohmann::json createObject(
        const std::string& document,
        const std::string& type,
        const std::optional<std::string>& name,
        const nlohmann::json& properties,
        const nlohmann::json& view
    );
    nlohmann::json deleteObject(const std::string& document, const std::string& object);
    nlohmann::json dependencies(
        const std::string& document,
        const std::optional<std::string>& object
    ) const;

    /// The document a tool addresses: by name or label, or the active one.
    App::Document* resolveDocument(const std::string& nameOrLabel) const;
    App::DocumentObject* resolveObject(App::Document* doc, const std::string& nameOrLabel) const;
    std::string activeDocument() const;

    nlohmann::json documentInfo(const App::Document* doc) const;
    nlohmann::json objectSummary(const App::DocumentObject* obj) const;

    /// Saves a document to its file (or the default file in the project) and
    /// makes it one of the project's documents.
    nlohmann::json saveOwned(App::Document* doc);

    /// With autoSave: saves every open document that belongs to the project
    /// (its file is inside the project folder, or it has no file yet, in
    /// which case it is adopted). Returns the list of saves.
    nlohmann::json saveProjectDocuments();

private:
    struct Call
    {
        std::uint64_t messagesFrom;
    };
    Call begin() const;
    nlohmann::json finish(const Call& call, nlohmann::json result) const;
    nlohmann::json recomputeNow(App::Document* doc, bool force) const;
    nlohmann::json autoSave(App::Document* doc) const;
    void track(App::Document* doc);
    void pruneClosed() const;
    void notify(const App::Document* doc, const char* event) const;
    void record(
        const std::string& kind,
        const std::string& summary,
        const std::string& python,
        nlohmann::json detail
    ) const;
    std::string relativeFile(const App::Document* doc) const;
    std::string defaultFile(const App::Document* doc) const;

    EngineSession& session;
    MessageLog& messages;
    mutable std::vector<std::string> owned;
    mutable std::string active;
};

}  // namespace Agentic
