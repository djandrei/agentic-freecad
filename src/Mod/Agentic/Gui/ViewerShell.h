// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>

#include <QObject>

#include <Mod/Agentic/AgenticGlobal.h>
#include <Mod/Agentic/App/SessionObserver.h>

class QPlainTextEdit;

namespace Gui
{
class MainWindow;
}

namespace Agentic
{

/// Turns the upstream main window into the agent viewer.
///
/// Nothing in Gui::MainWindow is modified: the shell uses the public Qt and
/// FreeCAD API to hide the menu bar, the toolbars and the upstream dock
/// windows, closes pages that belong to no document (the Start page), puts
/// the 3D views in tabbed mode so the active one fills the window, and
/// follows the document the agent works on. The 3D views stay where the GUI
/// layer creates them, inside the window's MDI area.
class AgenticGuiExport ViewerShell: public QObject, public SessionObserver
{
    Q_OBJECT

public:
    explicit ViewerShell(Gui::MainWindow* mainWindow);
    ~ViewerShell() override;

    /// Applies the agent-mode appearance. Safe to call more than once.
    void apply();

    /// Closes MDI pages that belong to no document.
    void tidyViews();

    /// Brings a document's 3D view to the front.
    void showDocument(const std::string& document);

    /// Appends one line to the Activity panel.
    void appendActivity(const std::string& line);

    // SessionObserver
    void documentChanged(
        const Project& project,
        const std::string& document,
        const std::string& event
    ) override;

private:
    Gui::MainWindow* mainWindow;
    QPlainTextEdit* activity {nullptr};
    bool applied {false};
};

}  // namespace Agentic
