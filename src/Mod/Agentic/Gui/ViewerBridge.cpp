// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <QDateTime>
#include <QHeaderView>
#include <QTreeWidget>

#include <Mod/Agentic/App/ChangeLog.h>
#include <Mod/Agentic/App/Project.h>
#include <Mod/Agentic/App/Runtime.h>

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
{
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
    rebuild();
}

void ViewerBridge::changeLogged(const Project& project, const ChangeEntry& entry)
{
    History& history = histories[project.root().string()];
    history.changes.push_back(entry.toJson());
    if (history.changes.size() > maxRows) {
        history.changes.erase(history.changes.begin());
    }
    if (project.root().string() == currentRoot) {
        showChange(history.changes.back());
    }
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
    return {
        {"project", currentRoot.empty() ? json() : json(currentRoot)},
        {"activity", activityRows},
        {"changes", changeRows},
    };
}

}  // namespace Agentic
