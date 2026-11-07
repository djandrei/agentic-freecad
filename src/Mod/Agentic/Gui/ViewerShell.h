// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <map>
#include <string>

#include <QObject>
#include <QString>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>
#include <Mod/Agentic/App/SessionObserver.h>

class QAction;
class QDockWidget;
class QToolButton;
class QWidget;

namespace Gui
{
class MainWindow;
}

namespace Agentic
{

class ViewerBridge;

/// Turns the upstream main window into the agent viewer.
///
/// Nothing in Gui::MainWindow is modified: the shell uses the public Qt and
/// FreeCAD API to hide the menu bar, the toolbars and the upstream dock
/// windows, closes pages that belong to no document (the Start page), puts
/// the 3D views in tabbed mode so the active one fills the window, and
/// follows the document the agent works on. It docks the bridge's panels,
/// each closable and toggled from the status bar, with an alert dot on the
/// toggle of a hidden panel that got new content; F11 (View only) hides them
/// all and restores the same set. The layout persists between starts.
class AgenticGuiExport ViewerShell: public QObject, public SessionObserver
{
    Q_OBJECT

public:
    explicit ViewerShell(Gui::MainWindow* mainWindow);
    ~ViewerShell() override;

    static ViewerShell* instance();

    /// Applies the agent-mode appearance. Safe to call more than once.
    void apply();

    /// Closes MDI pages that belong to no document.
    void tidyViews();

    /// Brings a document's 3D view to the front.
    void showDocument(const std::string& document);

    /// Hides every panel (remembering which were shown), or shows them again.
    void setViewOnly(bool viewOnly);

    /// Panels (visible, alert) and View only, for viewerState().
    nlohmann::json panelState() const;

    // SessionObserver
    void documentChanged(
        const Project& project,
        const std::string& document,
        const std::string& event
    ) override;

private:
    struct Panel
    {
        QDockWidget* dock {nullptr};
        QToolButton* toggle {nullptr};
        QString title;
        int alert {0};  // 0 none, 1 new content, 2 failure
    };

    void addPanel(QWidget* widget, const QString& title, const char* objectName, Qt::DockWidgetArea area);
    void onContent(const QString& panel, bool alert);
    void showAlert(Panel& panel);
    void restoreLayout();
    void saveLayout() const;

    Gui::MainWindow* mainWindow;
    ViewerBridge* bridge {nullptr};
    std::map<QString, Panel> panels;
    std::map<QString, bool> beforeViewOnly;
    QAction* viewOnlyAction {nullptr};
    bool viewOnly {false};
    bool applied {false};
    bool restoring {false};
};

}  // namespace Agentic
