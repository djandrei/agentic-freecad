// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <algorithm>
#include <filesystem>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Base/Interpreter.h>

#include "DocumentEngine.h"
#include "EngineSession.h"
#include "MessageLog.h"
#include "PyBridge.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Agentic
{

namespace
{

json names(const std::vector<App::DocumentObject*>& objects)
{
    json list = json::array();
    for (const auto* obj : objects) {
        if (obj && obj->isAttachedToDocument()) {
            list.push_back(obj->getNameInDocument());
        }
    }
    return list;
}

std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

}  // namespace

DocumentEngine::DocumentEngine(EngineSession& session, MessageLog& messages)
    : session(session)
    , messages(messages)
{}

// ---------------------------------------------------------------- bookkeeping

DocumentEngine::Call DocumentEngine::begin() const
{
    return Call {messages.nextSeq()};
}

json DocumentEngine::finish(const Call& call, json result) const
{
    json list = json::array();
    int errors = 0;
    int warnings = 0;
    for (const auto& entry : messages.since(call.messagesFrom)) {
        if (entry.level == "log") {
            continue;
        }
        list.push_back({{"level", entry.level}, {"text", entry.text}});
        if (entry.level == "error" || entry.level == "critical") {
            ++errors;
        }
        else if (entry.level == "warning") {
            ++warnings;
        }
    }
    result["messages"] = list;
    result["errors"] = errors;
    result["warnings"] = warnings;
    return result;
}

void DocumentEngine::pruneClosed() const
{
    App::Application& app = App::GetApplication();
    owned.erase(
        std::remove_if(
            owned.begin(),
            owned.end(),
            [&app](const std::string& name) { return app.getDocument(name.c_str()) == nullptr; }
        ),
        owned.end()
    );
    if (!active.empty() && !app.getDocument(active.c_str())) {
        active = owned.empty() ? std::string() : owned.back();
    }
}

void DocumentEngine::track(App::Document* doc)
{
    const std::string name = doc->getName();
    if (std::find(owned.begin(), owned.end(), name) == owned.end()) {
        owned.push_back(name);
    }
    active = name;
    App::GetApplication().setActiveDocument(doc);
}

void DocumentEngine::notify(const App::Document* doc, const char* event) const
{
    const std::string name = doc ? doc->getName() : std::string();
    session.notifyDocument(name, event);
}

void DocumentEngine::record(
    const std::string& kind,
    const std::string& summary,
    const std::string& python,
    json detail
) const
{
    ChangeEntry entry;
    entry.kind = kind;
    entry.summary = summary;
    entry.python = python;
    entry.detail = std::move(detail);
    session.recordChange(std::move(entry));
}

std::string DocumentEngine::relativeFile(const App::Document* doc) const
{
    const std::string file = doc->FileName.getValue();
    if (file.empty()) {
        return {};
    }
    std::error_code ec;
    const fs::path rel = fs::path(file).lexically_relative(session.project().root());
    if (rel.empty() || rel.string().starts_with("..")) {
        return file;
    }
    return rel.generic_string();
}

std::string DocumentEngine::defaultFile(const App::Document* doc) const
{
    return (session.project().root() / (std::string(doc->getName()) + ".FCStd")).string();
}

App::Document* DocumentEngine::resolveDocument(const std::string& nameOrLabel) const
{
    pruneClosed();
    App::Application& app = App::GetApplication();
    if (nameOrLabel.empty()) {
        if (active.empty()) {
            throw std::runtime_error(
                "no active document in this project; call document_new or document_open"
            );
        }
        return app.getDocument(active.c_str());
    }
    if (App::Document* doc = app.getDocument(nameOrLabel.c_str())) {
        return doc;
    }
    for (App::Document* doc : app.getDocuments()) {
        if (nameOrLabel == doc->Label.getValue()) {
            return doc;
        }
    }
    throw std::invalid_argument("no document named " + nameOrLabel);
}

App::DocumentObject* DocumentEngine::resolveObject(App::Document* doc, const std::string& nameOrLabel) const
{
    if (App::DocumentObject* obj = doc->getObject(nameOrLabel.c_str())) {
        return obj;
    }
    App::DocumentObject* byLabel = nullptr;
    for (App::DocumentObject* obj : doc->getObjects()) {
        if (nameOrLabel == obj->Label.getValue()) {
            if (byLabel) {
                throw std::invalid_argument("label is not unique: " + nameOrLabel + "; use the Name");
            }
            byLabel = obj;
        }
    }
    if (!byLabel) {
        throw std::invalid_argument("no object named " + nameOrLabel + " in " + doc->getName());
    }
    return byLabel;
}

std::string DocumentEngine::activeDocument() const
{
    pruneClosed();
    return active;
}

json DocumentEngine::documentInfo(const App::Document* doc) const
{
    int errors = 0;
    for (const auto* obj : doc->getObjects()) {
        if (obj->isError()) {
            ++errors;
        }
    }
    const std::string file = relativeFile(doc);
    return {
        {"name", doc->getName()},
        {"label", doc->Label.getValue()},
        {"file", file.empty() ? json() : json(file)},
        {"saved", doc->isSaved()},
        {"touched", doc->isTouched()},
        {"objects", doc->countObjects()},
        {"objectsInError", errors},
        {"undo", doc->getAvailableUndos()},
        {"redo", doc->getAvailableRedos()},
        {"active", doc->getName() == active},
    };
}

json DocumentEngine::objectSummary(const App::DocumentObject* obj) const
{
    return {
        {"name", obj->getNameInDocument()},
        {"label", obj->Label.getValue()},
        {"type", obj->getTypeId().getName()},
        {"touched", obj->isTouched()},
        {"error", obj->isError()},
        {"valid", obj->isValid()},
        {"status", obj->getStatusString()},
        {"visible", obj->Visibility.getValue()},
        {"inList", names(obj->getInList())},
        {"outList", names(obj->getOutList())},
    };
}

// ------------------------------------------------------------------ documents

json DocumentEngine::listDocuments() const
{
    pruneClosed();
    App::Application& app = App::GetApplication();
    json open = json::array();
    std::vector<std::string> openFiles;
    for (const std::string& name : owned) {
        if (App::Document* doc = app.getDocument(name.c_str())) {
            open.push_back(documentInfo(doc));
            openFiles.push_back(relativeFile(doc));
        }
    }
    json files = json::array();
    for (const std::string& file : session.project().documents()) {
        files.push_back({
            {"file", file},
            {"open", std::find(openFiles.begin(), openFiles.end(), file) != openFiles.end()},
        });
    }
    return {{"documents", open}, {"files", files}, {"active", active.empty() ? json() : json(active)}};
}

json DocumentEngine::openDocument(const std::string& file)
{
    const Call call = begin();
    const fs::path full = session.resolvePath(file, true);
    App::Document* doc = App::GetApplication().openDocument(full.string().c_str());
    if (!doc) {
        throw std::runtime_error("FreeCAD could not open " + file);
    }
    track(doc);
    notify(doc, "opened");
    record(
        "document",
        "opened " + file,
        "App.openDocument(" + quoted(full.string()) + ")",
        {{"document", doc->getName()}, {"file", file}}
    );
    return finish(call, documentInfo(doc));
}

json DocumentEngine::newDocument(const std::string& name, const std::optional<std::string>& file)
{
    const Call call = begin();
    App::Document* doc = App::GetApplication().newDocument(
        name.empty() ? nullptr : name.c_str(),
        name.empty() ? nullptr : name.c_str()
    );
    if (!doc) {
        throw std::runtime_error("FreeCAD could not create a document");
    }
    const fs::path target = file ? session.resolvePath(*file, false) : fs::path(defaultFile(doc));
    doc->FileName.setValue(target.string());
    track(doc);
    notify(doc, "created");
    record(
        "document",
        std::string("created document ") + doc->getName(),
        "doc = App.newDocument(" + quoted(doc->getName()) + ")",
        {{"document", doc->getName()}, {"file", relativeFile(doc)}}
    );
    json result = documentInfo(doc);
    if (session.project().settings().autoSave) {
        result["saved"] = autoSave(doc)["saved"];
    }
    return finish(call, result);
}

json DocumentEngine::autoSave(App::Document* doc) const
{
    std::string file = doc->FileName.getValue();
    if (file.empty()) {
        file = defaultFile(doc);
        doc->FileName.setValue(file);
    }
    const bool ok = doc->saveAs(file.c_str());
    if (ok) {
        notify(doc, "saved");
    }
    else {
        Base::Console().warning("Agentic: could not save {}\n", file);
    }
    return {{"saved", ok}, {"file", relativeFile(doc)}};
}

json DocumentEngine::saveOwned(App::Document* doc)
{
    const std::string name = doc->getName();
    if (std::find(owned.begin(), owned.end(), name) == owned.end()) {
        owned.push_back(name);
    }
    if (active.empty()) {
        active = name;
    }
    json out = autoSave(doc);
    out["document"] = name;
    return out;
}

json DocumentEngine::saveDocument(const std::string& document, const std::optional<std::string>& as)
{
    const Call call = begin();
    App::Document* doc = resolveDocument(document);
    if (as) {
        const fs::path target = session.resolvePath(*as, false);
        doc->FileName.setValue(target.string());
    }
    json saved = autoSave(doc);
    if (!saved["saved"].get<bool>()) {
        throw std::runtime_error(std::string("could not save ") + doc->getName());
    }
    record(
        "document",
        std::string("saved ") + doc->getName() + " to " + relativeFile(doc),
        "App.getDocument(" + quoted(doc->getName()) + ").saveAs(" + quoted(doc->FileName.getValue())
            + ")",
        {{"document", doc->getName()}, {"file", relativeFile(doc)}}
    );
    return finish(call, documentInfo(doc));
}

json DocumentEngine::closeDocument(const std::string& document)
{
    const Call call = begin();
    App::Document* doc = resolveDocument(document);
    const std::string name = doc->getName();
    App::GetApplication().closeDocument(name.c_str());
    owned.erase(std::remove(owned.begin(), owned.end(), name), owned.end());
    if (active == name) {
        active = owned.empty() ? std::string() : owned.back();
    }
    session.notifyDocument(name, "closed");
    record("document", "closed " + name, "App.closeDocument(" + quoted(name) + ")", {{"document", name}});
    return finish(
        call,
        {{"document", name}, {"closed", true}, {"active", active.empty() ? json() : json(active)}}
    );
}

json DocumentEngine::recomputeNow(App::Document* doc, bool force) const
{
    bool hasError = false;
    const int count = doc->recompute({}, force, &hasError);
    json errors = json::array();
    json touched = json::array();
    for (const auto* obj : doc->getObjects()) {
        if (obj->isError()) {
            errors.push_back({
                {"name", obj->getNameInDocument()},
                {"label", obj->Label.getValue()},
                {"type", obj->getTypeId().getName()},
                {"status", obj->getStatusString()},
            });
        }
        else if (obj->isTouched()) {
            touched.push_back(obj->getNameInDocument());
        }
    }
    notify(doc, "recomputed");
    return {
        {"recomputed", count},
        {"hasError", hasError || !errors.empty()},
        {"objectsInError", errors},
        {"stillTouched", touched}
    };
}

json DocumentEngine::recompute(const std::string& document, bool force)
{
    const Call call = begin();
    App::Document* doc = resolveDocument(document);
    json result = recomputeNow(doc, force);
    record(
        "document",
        std::string("recomputed ") + doc->getName(),
        "App.getDocument(" + quoted(doc->getName()) + ").recompute()",
        {{"document", doc->getName()}, {"hasError", result["hasError"]}}
    );
    if (session.project().settings().autoSave && !doc->FileName.getStrValue().empty()) {
        result["saved"] = autoSave(doc)["saved"];
    }
    result["document"] = doc->getName();
    return finish(call, result);
}

json DocumentEngine::undo(const std::string& document)
{
    const Call call = begin();
    App::Document* doc = resolveDocument(document);
    const auto before = doc->getAvailableUndoNames();
    const bool done = doc->undo();
    notify(doc, "undo");
    record(
        "document",
        std::string("undo ") + (before.empty() ? "" : before.front()),
        "App.getDocument(" + quoted(doc->getName()) + ").undo()",
        {{"document", doc->getName()}}
    );
    return finish(
        call,
        {
            {"document", doc->getName()},
            {"undone", done},
            {"transaction", before.empty() ? json() : json(before.front())},
            {"undo", doc->getAvailableUndoNames()},
            {"redo", doc->getAvailableRedoNames()},
        }
    );
}

json DocumentEngine::redo(const std::string& document)
{
    const Call call = begin();
    App::Document* doc = resolveDocument(document);
    const auto before = doc->getAvailableRedoNames();
    const bool done = doc->redo();
    notify(doc, "redo");
    record(
        "document",
        std::string("redo ") + (before.empty() ? "" : before.front()),
        "App.getDocument(" + quoted(doc->getName()) + ").redo()",
        {{"document", doc->getName()}}
    );
    return finish(
        call,
        {
            {"document", doc->getName()},
            {"redone", done},
            {"transaction", before.empty() ? json() : json(before.front())},
            {"undo", doc->getAvailableUndoNames()},
            {"redo", doc->getAvailableRedoNames()},
        }
    );
}

// -------------------------------------------------------------------- objects

json DocumentEngine::listObjects(const std::string& document, const std::optional<std::string>& type) const
{
    App::Document* doc = resolveDocument(document);
    json list = json::array();
    for (const auto* obj : doc->getObjects()) {
        if (type && !obj->getTypeId().isDerivedFrom(Base::Type::fromName(type->c_str()))) {
            continue;
        }
        list.push_back(objectSummary(obj));
    }
    return {{"document", doc->getName()}, {"objects", list}};
}

json DocumentEngine::getObject(
    const std::string& document,
    const std::string& object,
    const json& properties,
    bool includeView
) const
{
    App::Document* doc = resolveDocument(document);
    App::DocumentObject* obj = resolveObject(doc, object);
    json args = {
        {"document", doc->getName()},
        {"object", obj->getNameInDocument()},
        {"properties", properties.is_array() && !properties.empty() ? properties : json()},
        {"view", includeView},
    };
    return callPython("agentic_props", "object_info", args);
}

json DocumentEngine::setObject(
    const std::string& document,
    const std::string& object,
    const json& properties,
    const json& view
)
{
    const Call call = begin();
    App::Document* doc = resolveDocument(document);
    App::DocumentObject* obj = resolveObject(doc, object);
    json args = {
        {"document", doc->getName()},
        {"object", obj->getNameInDocument()},
        {"properties", properties.is_object() ? properties : json::object()},
        {"view", view.is_object() ? view : json::object()},
    };
    doc->openTransaction(std::string("object_set ") + obj->getNameInDocument());
    json changed;
    try {
        changed = callPython("agentic_props", "set_properties", args);
        doc->commitTransaction();
    }
    catch (...) {
        doc->abortTransaction();
        throw;
    }
    std::string python;
    for (const auto& line : changed["python"]) {
        python += line.get<std::string>() + "\n";
    }
    record(
        "property",
        std::string("set ") + obj->getNameInDocument() + ": " + changed["changed"].dump(),
        python,
        {{"document", doc->getName()},
         {"object", obj->getNameInDocument()},
         {"changed", changed["changed"]}}
    );
    json result = {
        {"document", doc->getName()},
        {"object", obj->getNameInDocument()},
        {"changed", changed["changed"]}
    };
    if (session.project().settings().autoRecompute) {
        result["recompute"] = recomputeNow(doc, false);
    }
    if (session.project().settings().autoSave) {
        result["saved"] = autoSave(doc)["saved"];
    }
    result["status"] = objectSummary(obj);
    return finish(call, result);
}

json DocumentEngine::createObject(
    const std::string& document,
    const std::string& type,
    const std::optional<std::string>& name,
    const json& properties,
    const json& view
)
{
    const Call call = begin();
    App::Document* doc = resolveDocument(document);
    Base::Type typeId = Base::Type::fromName(type.c_str());
    if (typeId.isBad()) {
        // Types are registered when their module is imported; try the module
        // named by the prefix (Part::Box -> import Part).
        const auto sep = type.find("::");
        if (sep != std::string::npos) {
            const std::string module = type.substr(0, sep);
            try {
                Base::PyGILStateLocker lock;
                Base::Interpreter().loadModule(module.c_str());
            }
            catch (const Base::Exception&) {
                // reported below as an unknown type
            }
            typeId = Base::Type::fromName(type.c_str());
        }
    }
    if (typeId.isBad() || !typeId.isDerivedFrom(App::DocumentObject::getClassTypeId())) {
        throw std::invalid_argument("unknown object type: " + type);
    }
    doc->openTransaction("object_create " + type);
    App::DocumentObject* obj = nullptr;
    json changed = json::array();
    try {
        obj = doc->addObject(type, name ? name->c_str() : nullptr);
        if (!obj) {
            throw std::runtime_error("FreeCAD refused to create " + type);
        }
        if ((properties.is_object() && !properties.empty()) || (view.is_object() && !view.empty())) {
            json args = {
                {"document", doc->getName()},
                {"object", obj->getNameInDocument()},
                {"properties", properties.is_object() ? properties : json::object()},
                {"view", view.is_object() ? view : json::object()},
            };
            changed = callPython("agentic_props", "set_properties", args);
        }
        doc->commitTransaction();
    }
    catch (...) {
        doc->abortTransaction();
        throw;
    }
    std::string python = std::string("obj = App.getDocument(") + quoted(doc->getName())
        + ").addObject(" + quoted(type) + ", " + quoted(obj->getNameInDocument()) + ")\n";
    if (changed.is_object()) {
        for (const auto& line : changed["python"]) {
            python += line.get<std::string>() + "\n";
        }
    }
    record(
        "object",
        std::string("created ") + type + " " + obj->getNameInDocument(),
        python,
        {{"document", doc->getName()}, {"object", obj->getNameInDocument()}, {"type", type}}
    );
    json result = {
        {"document", doc->getName()},
        {"object", obj->getNameInDocument()},
        {"type", type},
        {"changed", changed.is_object() ? changed["changed"] : json::array()},
    };
    if (session.project().settings().autoRecompute) {
        result["recompute"] = recomputeNow(doc, false);
    }
    if (session.project().settings().autoSave) {
        result["saved"] = autoSave(doc)["saved"];
    }
    result["status"] = objectSummary(obj);
    return finish(call, result);
}

json DocumentEngine::deleteObject(const std::string& document, const std::string& object)
{
    const Call call = begin();
    App::Document* doc = resolveDocument(document);
    App::DocumentObject* obj = resolveObject(doc, object);
    const std::string name = obj->getNameInDocument();
    const json dependents = names(obj->getInList());
    doc->openTransaction("object_delete " + name);
    try {
        doc->removeObject(name.c_str());
        doc->commitTransaction();
    }
    catch (...) {
        doc->abortTransaction();
        throw;
    }
    record(
        "object",
        "deleted " + name,
        "App.getDocument(" + quoted(doc->getName()) + ").removeObject(" + quoted(name) + ")",
        {{"document", doc->getName()}, {"object", name}}
    );
    notify(doc, "objects");
    json result = {
        {"document", doc->getName()},
        {"object", name},
        {"deleted", true},
        {"dependentsTouched", dependents}
    };
    if (session.project().settings().autoRecompute) {
        result["recompute"] = recomputeNow(doc, false);
    }
    if (session.project().settings().autoSave) {
        result["saved"] = autoSave(doc)["saved"];
    }
    return finish(call, result);
}

json DocumentEngine::dependencies(
    const std::string& document,
    const std::optional<std::string>& object
) const
{
    App::Document* doc = resolveDocument(document);
    json edges = json::array();
    json nodes = json::array();
    std::vector<App::DocumentObject*> objects;
    if (object) {
        App::DocumentObject* obj = resolveObject(doc, *object);
        objects.push_back(obj);
        for (auto* o : obj->getInListRecursive()) {
            objects.push_back(o);
        }
        for (auto* o : obj->getOutListRecursive()) {
            objects.push_back(o);
        }
    }
    else {
        objects = doc->getObjects();
    }
    std::sort(objects.begin(), objects.end());
    objects.erase(std::unique(objects.begin(), objects.end()), objects.end());
    for (const auto* obj : objects) {
        nodes.push_back(
            {{"name", obj->getNameInDocument()},
             {"label", obj->Label.getValue()},
             {"type", obj->getTypeId().getName()},
             {"error", obj->isError()}}
        );
        for (const auto* dep : obj->getOutList()) {
            if (dep && dep->isAttachedToDocument()) {
                edges.push_back({{"from", obj->getNameInDocument()}, {"to", dep->getNameInDocument()}});
            }
        }
    }
    return {{"document", doc->getName()}, {"objects", nodes}, {"dependsOn", edges}};
}

}  // namespace Agentic
