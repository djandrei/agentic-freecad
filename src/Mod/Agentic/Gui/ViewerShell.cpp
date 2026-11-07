// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Base/Console.h>
#include <Gui/MainWindow.h>

#include <Mod/Agentic/App/Runtime.h>

#include "ViewerShell.h"

namespace Agentic
{

ViewerShell::ViewerShell(Gui::MainWindow* mainWindow)
    : QObject(mainWindow)
    , mainWindow(mainWindow)
{}

void ViewerShell::apply()
{
    if (applied) {
        return;
    }
    applied = true;

    const AgentConfig& cfg = Runtime::instance().config();

    mainWindow->setWindowTitle(QStringLiteral("FreeCAD (agent mode)"));

    if (QMenuBar* menuBar = mainWindow->menuBar()) {
        menuBar->hide();
    }
    for (QToolBar* toolBar : mainWindow->findChildren<QToolBar*>()) {
        toolBar->hide();
    }
    for (QDockWidget* dock : mainWindow->findChildren<QDockWidget*>()) {
        dock->hide();
    }

    activity = new QPlainTextEdit(mainWindow);
    activity->setReadOnly(true);
    activity->setMaximumBlockCount(2000);
    auto dock = new QDockWidget(QStringLiteral("Activity"), mainWindow);
    dock->setObjectName(QStringLiteral("Agentic_Activity"));
    dock->setWidget(activity);
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);

    QString transport = QStringLiteral("none");
    if (cfg.stdio) {
        transport = QStringLiteral("stdio");
    }
    else if (cfg.port > 0) {
        transport = QStringLiteral("http:%1").arg(cfg.port);
    }
    auto label = new QLabel(QStringLiteral("Agent mode - %1").arg(transport), mainWindow);
    mainWindow->statusBar()->addPermanentWidget(label);

    Base::Console().log("Agentic: viewer shell applied\n");
}

void ViewerShell::appendActivity(const std::string& line)
{
    if (activity) {
        activity->appendPlainText(QString::fromStdString(line));
    }
}

}  // namespace Agentic
