// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <QMdiArea>
#include <QMdiSubWindow>
#include <QTreeWidget>

#include <App/Document.h>
#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/View3DInventor.h>

#include <Mod/Agentic/App/Runtime.h>

#include "ViewerBridge.h"
#include "ViewerShell.h"

namespace Agentic
{

ViewerShell::ViewerShell(Gui::MainWindow* mainWindow)
    : QObject(mainWindow)
    , mainWindow(mainWindow)
{
    Runtime::instance().observers().add(this);
}

ViewerShell::~ViewerShell()
{
    Runtime::instance().observers().remove(this);
}

void ViewerShell::apply()
{
    if (applied) {
        return;
    }
    applied = true;

    const AgentConfig& cfg = Runtime::instance().config();

    if (QMenuBar* menuBar = mainWindow->menuBar()) {
        menuBar->hide();
    }
    for (QToolBar* toolBar : mainWindow->findChildren<QToolBar*>()) {
        toolBar->hide();
    }
    for (QDockWidget* dock : mainWindow->findChildren<QDockWidget*>()) {
        dock->hide();
    }
    if (QMdiArea* mdi = mainWindow->getMdiArea()) {
        mdi->setViewMode(QMdiArea::TabbedView);
    }

    bridge = new ViewerBridge(this);
    addPanel(bridge->activityPanel(), tr("Activity"), "Agentic_Activity", Qt::BottomDockWidgetArea);
    addPanel(bridge->changesPanel(), tr("Changes"), "Agentic_Changes", Qt::BottomDockWidgetArea);

    const QString transport = cfg.stdio ? QStringLiteral("stdio") : QStringLiteral("http");
    // The window title belongs to the main window, which rewrites it whenever
    // the active page changes; the status bar says agent mode instead.
    auto label = new QLabel(QStringLiteral("Agent mode - %1").arg(transport), mainWindow);
    label->setObjectName(QStringLiteral("Agentic_ModeLabel"));
    mainWindow->statusBar()->addPermanentWidget(label);

    // Upstream modules (the Start page) act on guiInitialized, which may come
    // after this; tidy again then.
    connect(mainWindow, &Gui::MainWindow::guiInitialized, this, &ViewerShell::tidyViews);
    tidyViews();

    Base::Console().log("Agentic: viewer shell applied\n");
}

void ViewerShell::tidyViews()
{
    QMdiArea* mdi = mainWindow->getMdiArea();
    if (!mdi) {
        return;
    }
    for (QMdiSubWindow* sub : mdi->subWindowList()) {
        auto view = qobject_cast<Gui::MDIView*>(sub->widget());
        if (!view || !view->getGuiDocument()) {
            sub->close();
        }
    }
}

void ViewerShell::showDocument(const std::string& document)
{
    if (!applied) {
        return;
    }
    tidyViews();
    Gui::Document* gdoc = Gui::Application::Instance->getDocument(document.c_str());
    if (!gdoc) {
        return;
    }
    const auto views = gdoc->getMDIViewsOfType(Gui::View3DInventor::getClassTypeId());
    if (!views.empty()) {
        mainWindow->setActiveWindow(views.front());
    }
}

void ViewerShell::documentChanged(
    const Project& /*project*/,
    const std::string& document,
    const std::string& event
)
{
    if (event == "closed") {
        return;
    }
    // Queued: the document's view may be created later in this event-loop turn.
    QTimer::singleShot(0, this, [this, document]() { showDocument(document); });
}

void ViewerShell::addPanel(
    QWidget* widget,
    const QString& title,
    const char* objectName,
    Qt::DockWidgetArea area
)
{
    auto dock = new QDockWidget(title, mainWindow);
    dock->setObjectName(QString::fromLatin1(objectName));
    dock->setWidget(widget);
    mainWindow->addDockWidget(area, dock);
}

}  // namespace Agentic
