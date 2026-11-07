// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <QDateTime>
#include <QHeaderView>
#include <QTreeWidget>

#include <functional>
#include <set>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/Application.h>
#include <Gui/ViewProviderDocumentObject.h>

#include <Mod/Agentic/App/ChangeLog.h>
#include <Mod/Agentic/App/DocumentEngine.h>
#include <Mod/Agentic/App/EngineSession.h>
#include <Mod/Agentic/App/Project.h>
#include <Mod/Agentic/App/PyBridge.h>
#include <Mod/Agentic/App/Runtime.h>
#include <Mod/Agentic/App/SessionManager.h>

#include "ViewerBridge.h"

using json = nlohmann::json;

namespace Agentic
{

namespace
{

ViewerBridge* bridgeInstance = nullptr;

QTreeWidget* makeTable(const QStringList& headers, const char* objectName)
{
    auto tree = new QTreeWidget();
    tree->setObjectName(QString::fromLatin1(objectName));
    tree->setHeaderLabels(headers);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->header()->setStretchLastSection(true);
    return tree;
}

std::string shorten(const std::string& text, std::size_t limit)
{
    if (text.size() <= limit) {
        return text;
    }
    return text.substr(0, limit - 3) + "...";
}

}  // namespace

ViewerBridge::ViewerBridge(QObject* parent)
    : QObject(parent)
    , activity(makeTable(
          {tr("Time"), tr("Tool"), tr("Arguments"), tr("ms"), tr("Result")},
          "Agentic_ActivityPanel"
      ))
    , changes(makeTable({tr("#"), tr("Kind"), tr("Change")}, "Agentic_ChangesPanel"))
    , model(new QTreeWidget())
    , properties(new QTreeWidget())
{
    model->setObjectName(QStringLiteral("Agentic_ModelPanel"));
    model->setHeaderLabels({tr("Model")});
    model->setEditTriggers(QAbstractItemView::NoEditTriggers);
    model->setDragDropMode(QAbstractItemView::NoDragDrop);
    model->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(model, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const QString object = item->data(0, Qt::UserRole + 1).toString();
        if (!object.isEmpty()) {
            showObject(item->data(0, Qt::UserRole).toString().toStdString(), object.toStdString());
        }
    });

    properties->setObjectName(QStringLiteral("Agentic_PropertiesPanel"));
    properties->setHeaderLabels({tr("Property"), tr("Value"), tr("Expression")});
    properties->setEditTriggers(QAbstractItemView::NoEditTriggers);
    properties->setSelectionMode(QAbstractItemView::NoSelection);
    properties->header()->setStretchLastSection(true);

    bridgeInstance = this;
    Runtime::instance().observers().add(this);
}

ViewerBridge::~ViewerBridge()
{
    Runtime::instance().observers().remove(this);
    if (bridgeInstance == this) {
        bridgeInstance = nullptr;
    }
}

ViewerBridge* ViewerBridge::instance()
{
    return bridgeInstance;
}

QTreeWidget* ViewerBridge::activityPanel() const
{
    return activity;
}

QTreeWidget* ViewerBridge::changesPanel() const
{
    return changes;
}

QTreeWidget* ViewerBridge::modelPanel() const
{
    return model;
}

QTreeWidget* ViewerBridge::propertiesPanel() const
{
    return properties;
}

void ViewerBridge::scheduleModel()
{
    if (modelPending) {
        return;
    }
    modelPending = true;
    // Coalesce the events of one tool call into one rebuild.
    QTimer::singleShot(0, this, [this]() {
        modelPending = false;
        rebuildModel();
        rebuildProperties();
    });
}

namespace
{

QTreeWidgetItem* objectItem(App::DocumentObject* obj, std::set<App::DocumentObject*>& placed, int depth)
{
    placed.insert(obj);
    auto item = new QTreeWidgetItem({QString::fromUtf8(obj->Label.getValue())});
    item->setData(0, Qt::UserRole, QString::fromLatin1(obj->getDocument()->getName()));
    item->setData(0, Qt::UserRole + 1, QString::fromLatin1(obj->getNameInDocument()));
    QString tip = QStringLiteral("%1 (%2)").arg(
        QString::fromLatin1(obj->getNameInDocument()),
        QString::fromLatin1(obj->getTypeId().getName())
    );
    auto vpd = dynamic_cast<Gui::ViewProviderDocumentObject*>(
        Gui::Application::Instance->getViewProvider(obj)
    );
    if (vpd) {
        item->setIcon(0, vpd->getIcon());
    }
    if (obj->isError()) {
        item->setForeground(0, QBrush(Qt::red));
        tip += QLatin1Char('\n') + QString::fromUtf8(obj->getStatusString());
    }
    else if (obj->isTouched()) {
        QFont font = item->font(0);
        font.setItalic(true);
        item->setFont(0, font);
        tip += QLatin1Char('\n') + QObject::tr("touched: needs a recompute");
    }
    if (!obj->Visibility.getValue() && !obj->isError()) {
        item->setForeground(0, QBrush(Qt::gray));
    }
    item->setToolTip(0, tip);
    if (vpd && depth < 32) {
        for (App::DocumentObject* child : vpd->claimChildren()) {
            if (child && child->isAttachedToDocument() && !placed.contains(child)) {
                item->addChild(objectItem(child, placed, depth + 1));
            }
        }
    }
    return item;
}

QString valueText(const nlohmann::json& value)
{
    if (value.is_object() && value.contains("string")) {
        return QString::fromStdString(value["string"].get<std::string>());
    }
    if (value.is_string()) {
        return QString::fromStdString(value.get<std::string>());
    }
    return QString::fromStdString(value.dump());
}

}  // namespace

void ViewerBridge::rebuildModel()
{
    model->clear();
    EngineSession* session = Runtime::instance().sessions().current();
    if (!session) {
        return;
    }
    const json docs = session->documents().listDocuments();
    QTreeWidgetItem* toSelect = nullptr;
    for (const auto& info : docs["documents"]) {
        App::Document* doc = App::GetApplication().getDocument(info["name"].get<std::string>().c_str());
        if (!doc) {
            continue;
        }
        auto docItem = new QTreeWidgetItem({QString::fromUtf8(doc->Label.getValue())});
        if (info["file"].is_string()) {
            docItem->setToolTip(0, QString::fromStdString(info["file"].get<std::string>()));
        }
        QFont font = docItem->font(0);
        font.setBold(info.value("active", false));
        docItem->setFont(0, font);
        model->addTopLevelItem(docItem);

        // Objects claimed by another object appear under it, as in the classic tree.
        std::set<App::DocumentObject*> claimed;
        for (App::DocumentObject* obj : doc->getObjects()) {
            auto vpd = dynamic_cast<Gui::ViewProviderDocumentObject*>(
                Gui::Application::Instance->getViewProvider(obj)
            );
            if (vpd) {
                for (App::DocumentObject* child : vpd->claimChildren()) {
                    if (child != obj) {
                        claimed.insert(child);
                    }
                }
            }
        }
        std::set<App::DocumentObject*> placed;
        for (App::DocumentObject* obj : doc->getObjects()) {
            if (!claimed.contains(obj) && !placed.contains(obj)) {
                docItem->addChild(objectItem(obj, placed, 0));
            }
        }
        if (doc->getName() == shownDocument) {
            for (QTreeWidgetItemIterator it(docItem); *it; ++it) {
                if ((*it)->data(0, Qt::UserRole + 1).toString().toStdString() == shownObject) {
                    toSelect = *it;
                    break;
                }
            }
        }
    }
    model->expandAll();
    if (toSelect) {
        model->setCurrentItem(toSelect);
        model->scrollToItem(toSelect);
    }
}

void ViewerBridge::showObject(const std::string& document, const std::string& object)
{
    shownDocument = document;
    shownObject = object;
    rebuildProperties();
    for (QTreeWidgetItemIterator it(model); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString().toStdString() == document
            && (*it)->data(0, Qt::UserRole + 1).toString().toStdString() == object) {
            model->setCurrentItem(*it);
            break;
        }
    }
}

void ViewerBridge::rebuildProperties()
{
    properties->clear();
    if (shownObject.empty()) {
        return;
    }
    App::Document* doc = App::GetApplication().getDocument(shownDocument.c_str());
    if (!doc || !doc->getObject(shownObject.c_str())) {
        shownObject.clear();
        return;
    }
    json info;
    try {
        info = callPython(
            "agentic_props",
            "object_info",
            {{"document", shownDocument}, {"object", shownObject}, {"view", true}}
        );
    }
    catch (const std::exception& e) {
        properties->addTopLevelItem(new QTreeWidgetItem({tr("error"), QString::fromUtf8(e.what())}));
        return;
    }
    auto addGroups = [this](const QString& prefix, const json& props) {
        std::map<std::string, QTreeWidgetItem*> groups;
        for (const auto& [name, prop] : props.items()) {
            if (prop.value("hidden", false)) {
                continue;
            }
            std::string group = prop.value("group", std::string());
            if (group.empty()) {
                group = "Base";
            }
            QTreeWidgetItem*& groupItem = groups[group];
            if (!groupItem) {
                QString title = QString::fromStdString(group);
                if (!prefix.isEmpty()) {
                    title = prefix + QStringLiteral(" - ") + title;
                }
                groupItem = new QTreeWidgetItem({title});
                QFont font = groupItem->font(0);
                font.setBold(true);
                groupItem->setFont(0, font);
                properties->addTopLevelItem(groupItem);
            }
            const QString text = valueText(prop["value"]);
            auto row = new QTreeWidgetItem({
                QString::fromStdString(name),
                text,
                QString::fromStdString(prop.value("expression", std::string())),
            });
            row->setToolTip(
                0,
                QString::fromStdString(
                    prop.value("type", std::string()) + "\n" + prop.value("doc", std::string())
                )
            );
            row->setToolTip(1, text);
            groupItem->addChild(row);
        }
    };
    auto header = new QTreeWidgetItem({
        QString::fromStdString(info.value("label", std::string())),
        QString::fromStdString(info.value("type", std::string())),
        QString::fromStdString(info.value("statusString", std::string())),
    });
    properties->addTopLevelItem(header);
    addGroups(QString(), info["properties"]);
    if (info.contains("view")) {
        addGroups(tr("View"), info["view"]);
    }
    properties->expandAll();
    properties->resizeColumnToContents(0);
}

json ViewerBridge::modelState() const
{
    std::function<json(const QTreeWidgetItem*)> describe = [&describe](const QTreeWidgetItem* item) {
        json children = json::array();
        for (int i = 0; i < item->childCount(); ++i) {
            children.push_back(describe(item->child(i)));
        }
        json node = {{"label", item->text(0).toStdString()}, {"children", children}};
        const QString name = item->data(0, Qt::UserRole + 1).toString();
        if (!name.isEmpty()) {
            node["name"] = name.toStdString();
            node["error"] = item->foreground(0).color() == QColor(Qt::red);
            node["touched"] = item->font(0).italic();
        }
        return node;
    };
    json out = json::array();
    for (int i = 0; i < model->topLevelItemCount(); ++i) {
        out.push_back(describe(model->topLevelItem(i)));
    }
    return out;
}

std::string ViewerBridge::summarize(const json& arguments)
{
    if (!arguments.is_object() || arguments.empty()) {
        return {};
    }
    std::string out;
    for (const auto& [key, value] : arguments.items()) {
        if (!out.empty()) {
            out += ", ";
        }
        std::string shown;
        if (value.is_string()) {
            const std::string text = value.get<std::string>();
            const bool bulky = key == "content" || key == "code"
                || text.find('\n') != std::string::npos;
            shown = bulky ? "<" + std::to_string(text.size()) + " chars>"
                          : json(shorten(text, 40)).dump();
        }
        else if (value.is_array() && value.size() > 4) {
            shown = "[" + std::to_string(value.size()) + " items]";
        }
        else {
            shown = shorten(value.dump(), 40);
        }
        out += key + "=" + shown;
    }
    return shorten(out, 160);
}

ViewerBridge::History& ViewerBridge::current()
{
    return histories[currentRoot];
}

void ViewerBridge::showActivity(const ActivityRow& row)
{
    auto item = new QTreeWidgetItem({
        QString::fromStdString(row.time),
        QString::fromStdString(row.tool),
        QString::fromStdString(row.arguments),
        QString::number(row.milliseconds, 'f', 0),
        row.ok ? tr("ok") : tr("error"),
    });
    if (!row.ok) {
        item->setForeground(4, QBrush(Qt::red));
    }
    activity->addTopLevelItem(item);
    while (activity->topLevelItemCount() > static_cast<int>(maxRows)) {
        delete activity->takeTopLevelItem(0);
    }
    activity->scrollToItem(item);
}

void ViewerBridge::showChange(const json& change)
{
    auto item = new QTreeWidgetItem({
        QString::number(change.value("seq", 0)),
        QString::fromStdString(change.value("kind", std::string())),
        QString::fromStdString(change.value("summary", std::string())),
    });
    const std::string python = change["python"].is_string() ? change["python"].get<std::string>()
                                                            : std::string();
    if (!python.empty()) {
        item->setToolTip(2, QString::fromStdString(python));
    }
    changes->addTopLevelItem(item);
    while (changes->topLevelItemCount() > static_cast<int>(maxRows)) {
        delete changes->takeTopLevelItem(0);
    }
    changes->scrollToItem(item);
}

void ViewerBridge::rebuild()
{
    activity->clear();
    changes->clear();
    const History& history = current();
    for (const auto& row : history.activity) {
        showActivity(row);
    }
    for (const auto& change : history.changes) {
        showChange(change);
    }
}

void ViewerBridge::currentProjectChanged(const Project* project)
{
    // Calls made before any project was open stay with the empty root.
    const std::string root = project ? project->root().string() : std::string();
    if (root == currentRoot) {
        return;
    }
    currentRoot = root;
    shownDocument.clear();
    shownObject.clear();
    rebuild();
    scheduleModel();
}

void ViewerBridge::documentChanged(
    const Project& /*project*/,
    const std::string& /*document*/,
    const std::string& /*event*/
)
{
    scheduleModel();
}

void ViewerBridge::changeLogged(const Project& project, const ChangeEntry& entry)
{
    History& history = histories[project.root().string()];
    history.changes.push_back(entry.toJson());
    if (history.changes.size() > maxRows) {
        history.changes.erase(history.changes.begin());
    }
    if (project.root().string() != currentRoot) {
        return;
    }
    showChange(history.changes.back());
    // The object the agent touched last becomes the one shown.
    const json& detail = entry.detail;
    std::string document;
    std::string object;
    if (detail.is_object() && detail.contains("object") && detail["object"].is_string()) {
        document = detail.value("document", std::string());
        object = detail["object"].get<std::string>();
    }
    else if (detail.is_object() && detail.contains("objects") && detail["objects"].is_object()) {
        for (const char* key : {"created", "changed"}) {
            const json list = detail["objects"].value(key, json::array());
            if (!list.empty()) {
                document = list.back().value("document", std::string());
                object = list.back().value("object", std::string());
                break;
            }
        }
    }
    if (!object.empty()) {
        if (document.empty()) {
            if (EngineSession* session = Runtime::instance().sessions().current()) {
                document = session->documents().activeDocument();
            }
        }
        shownDocument = document;
        shownObject = object;
    }
    scheduleModel();
}

void ViewerBridge::toolStarted(const std::string& /*name*/, const json& /*arguments*/)
{}

void ViewerBridge::toolCalled(const std::string& name, const json& arguments, double milliseconds, bool ok)
{
    ActivityRow row;
    row.time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")).toStdString();
    row.tool = name;
    row.arguments = summarize(arguments);
    row.milliseconds = milliseconds;
    row.ok = ok;
    // A call may have switched the project (project_open): the row belongs to
    // the project that is current when it finishes.
    History& history = current();
    history.activity.push_back(row);
    if (history.activity.size() > maxRows) {
        history.activity.erase(history.activity.begin());
    }
    showActivity(row);
}

json ViewerBridge::viewerState() const
{
    json activityRows = json::array();
    for (int i = 0; i < activity->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = activity->topLevelItem(i);
        activityRows.push_back({
            {"tool", item->text(1).toStdString()},
            {"arguments", item->text(2).toStdString()},
            {"ok", item->text(4) == tr("ok")},
        });
    }
    json changeRows = json::array();
    for (int i = 0; i < changes->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = changes->topLevelItem(i);
        changeRows.push_back(
            {{"kind", item->text(1).toStdString()}, {"summary", item->text(2).toStdString()}}
        );
    }
    json propertyRows = json::array();
    for (QTreeWidgetItemIterator it(properties); *it; ++it) {
        if ((*it)->parent() && (*it)->childCount() == 0) {
            propertyRows.push_back({
                {"group", (*it)->parent()->text(0).toStdString()},
                {"name", (*it)->text(0).toStdString()},
                {"value", (*it)->text(1).toStdString()},
                {"expression", (*it)->text(2).toStdString()},
            });
        }
    }
    const QTreeWidgetItem* selected = model->currentItem();
    return {
        {"project", currentRoot.empty() ? json() : json(currentRoot)},
        {"activity", activityRows},
        {"changes", changeRows},
        {"model", modelState()},
        {"selected",
         selected ? json(selected->data(0, Qt::UserRole + 1).toString().toStdString()) : json()},
        {"properties", {{"document", shownDocument}, {"object", shownObject}, {"rows", propertyRows}}},
    };
}

}  // namespace Agentic
