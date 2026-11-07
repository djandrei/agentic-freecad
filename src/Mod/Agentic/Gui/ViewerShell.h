// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>

#include <QObject>

#include <Mod/Agentic/AgenticGlobal.h>

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
/// windows, adds the viewer's own panels and marks the status bar. The 3D
/// views stay where the GUI layer creates them, inside the window's MDI area.
class AgenticGuiExport ViewerShell: public QObject
{
    Q_OBJECT

public:
    explicit ViewerShell(Gui::MainWindow* mainWindow);

    /// Applies the agent-mode appearance. Safe to call more than once.
    void apply();

    /// Appends one line to the Activity panel.
    void appendActivity(const std::string& line);

private:
    Gui::MainWindow* mainWindow;
    QPlainTextEdit* activity {nullptr};
    bool applied {false};
};

}  // namespace Agentic
