// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <map>
#include <string>
#include <vector>

#include <QObject>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>
#include <Mod/Agentic/App/SessionObserver.h>

class QLabel;
class QPlainTextEdit;
class QToolButton;
class QTreeWidget;

namespace Agentic
{

/// The viewer's side of the session: it observes the runtime and fills the
/// panels. It never drives the session.
///
/// Activity (tool calls) and changes are kept per project and the panels
/// show the current project's. viewerState() is the panels' content as
/// JSON, for tests and for anything else that wants to read the viewer.
class AgenticGuiExport ViewerBridge: public QObject, public SessionObserver
{
    Q_OBJECT

public:
    explicit ViewerBridge(QObject* parent = nullptr);
    ~ViewerBridge() override;

    static ViewerBridge* instance();

    QTreeWidget* activityPanel() const;
    QTreeWidget* changesPanel() const;
    QTreeWidget* modelPanel() const;
    QTreeWidget* propertiesPanel() const;
    QTreeWidget* messagesPanel() const;
    QPlainTextEdit* codePanel() const;

    /// Status-bar widgets, left to right: Pause, project, document, server.
    std::vector<QWidget*> statusWidgets() const;
    /// Refreshes the status-bar texts (project, document state, server URL).
    void updateStatus();

    /// Shows an object's properties (a person's inspection, or the agent's
    /// last touched object).
    void showObject(const std::string& document, const std::string& object);

    nlohmann::json viewerState() const;

Q_SIGNALS:
    /// A panel got new content; `alert` when it reports a failure (a failed
    /// call, an error message, an object in error).
    void contentChanged(const QString& panel, bool alert);

public:
    // SessionObserver
    void currentProjectChanged(const Project* project) override;
    void fileWritten(const Project& project, const WriteResult& result) override;
    void documentChanged(
        const Project& project,
        const std::string& document,
        const std::string& event
    ) override;
    void changeLogged(const Project& project, const ChangeEntry& entry) override;
    void toolStarted(const std::string& name, const nlohmann::json& arguments) override;
    void toolCalled(
        const std::string& name,
        const nlohmann::json& arguments,
        double milliseconds,
        bool ok
    ) override;

    /// A one-line summary of tool arguments: long strings shortened, file
    /// contents replaced by their length.
    static std::string summarize(const nlohmann::json& arguments);

private:
    struct ActivityRow
    {
        std::string time;
        std::string tool;
        std::string arguments;
        double milliseconds {0};
        bool ok {true};
    };
    struct History
    {
        std::vector<ActivityRow> activity;
        std::vector<nlohmann::json> changes;
    };

    History& current();
    void scheduleModel();
    void rebuildModel();
    void rebuildProperties();
    nlohmann::json modelState() const;
    void showCode(const std::string& title, const std::string& text, int firstLine, int lineCount);
    void showActivity(const ActivityRow& row);
    void showChange(const nlohmann::json& change);
    void rebuild();

    QTreeWidget* activity {nullptr};
    QTreeWidget* changes {nullptr};
    QTreeWidget* model {nullptr};
    QTreeWidget* properties {nullptr};
    QTreeWidget* messages {nullptr};
    QPlainTextEdit* code {nullptr};
    QToolButton* pause {nullptr};
    QLabel* projectLabel {nullptr};
    QLabel* documentLabel {nullptr};
    QLabel* serverLabel {nullptr};
    std::string codeTitle;
    std::uint64_t callMessagesFrom {0};
    bool modelPending {false};
    std::string shownDocument;
    std::string shownObject;
    std::map<std::string, History> histories;  // by project root
    std::string currentRoot;
    static constexpr std::size_t maxRows = 2000;
};

}  // namespace Agentic
