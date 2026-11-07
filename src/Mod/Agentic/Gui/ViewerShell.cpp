// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <QAction>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPlainTextEdit>
#include <QTreeWidget>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Console.h>
#include <Base/Parameter.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/View3DInventor.h>

#include <Mod/Agentic/App/Runtime.h>

#include "ViewerBridge.h"
#include "ViewerShell.h"

using json = nlohmann::json;

namespace Agentic
{

namespace
{

ViewerShell* shellInstance = nullptr;

Base::Reference<ParameterGrp> layoutParameters()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Agentic/Viewer"
    );
}

}  // namespace

ViewerShell::ViewerShell(Gui::MainWindow* mainWindow)
    : QObject(mainWindow)
    , mainWindow(mainWindow)
{
    shellInstance = this;
    Runtime::instance().observers().add(this);
}

ViewerShell::~ViewerShell()
{
    Runtime::instance().observers().remove(this);
    if (shellInstance == this) {
        shellInstance = nullptr;
    }
}

ViewerShell* ViewerShell::instance()
{
    return shellInstance;
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

    // The window title belongs to the main window, which rewrites it whenever
    // the active page changes; the status bar says agent mode instead.
    const QString transport = cfg.stdio ? QStringLiteral("stdio") : QStringLiteral("http");
    auto label = new QLabel(QStringLiteral("Agent mode - %1").arg(transport), mainWindow);
    label->setObjectName(QStringLiteral("Agentic_ModeLabel"));
    mainWindow->statusBar()->addPermanentWidget(label);

    bridge = new ViewerBridge(this);
    for (QWidget* widget : bridge->statusWidgets()) {
        mainWindow->statusBar()->addPermanentWidget(widget);
    }
    addPanel(bridge->modelPanel(), tr("Model"), "Agentic_Model", Qt::LeftDockWidgetArea);
    addPanel(bridge->propertiesPanel(), tr("Properties"), "Agentic_Properties", Qt::LeftDockWidgetArea);
    addPanel(bridge->activityPanel(), tr("Activity"), "Agentic_Activity", Qt::BottomDockWidgetArea);
    addPanel(bridge->changesPanel(), tr("Changes"), "Agentic_Changes", Qt::BottomDockWidgetArea);
    addPanel(bridge->messagesPanel(), tr("Messages"), "Agentic_Messages", Qt::BottomDockWidgetArea);
    addPanel(bridge->codePanel(), tr("Code"), "Agentic_Code", Qt::RightDockWidgetArea);
    connect(bridge, &ViewerBridge::contentChanged, this, &ViewerShell::onContent);

    viewOnlyAction = new QAction(tr("View only"), mainWindow);
    viewOnlyAction->setObjectName(QStringLiteral("Agentic_ViewOnly"));
    viewOnlyAction->setCheckable(true);
    viewOnlyAction->setShortcut(QKeySequence(Qt::Key_F11));
    viewOnlyAction->setShortcutContext(Qt::ApplicationShortcut);
    viewOnlyAction->setToolTip(tr("Hide every panel so the view fills the window (F11)"));
    connect(viewOnlyAction, &QAction::toggled, this, &ViewerShell::setViewOnly);
    mainWindow->addAction(viewOnlyAction);
    auto viewOnlyButton = new QToolButton(mainWindow);
    viewOnlyButton->setDefaultAction(viewOnlyAction);
    mainWindow->statusBar()->addPermanentWidget(viewOnlyButton);

    restoreLayout();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { saveLayout(); });

    // Upstream modules (the Start page) act on guiInitialized, which may come
    // after this; tidy again then.
    connect(mainWindow, &Gui::MainWindow::guiInitialized, this, &ViewerShell::tidyViews);
    tidyViews();

    Base::Console().log("Agentic: viewer shell applied\n");
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
    dock->setFeatures(QDockWidget::DockWidgetClosable);  // a fixed place, closable
    mainWindow->addDockWidget(area, dock);

    auto toggle = new QToolButton(mainWindow);
    toggle->setObjectName(QString::fromLatin1(objectName) + QStringLiteral("_Toggle"));
    toggle->setText(title);
    toggle->setCheckable(true);
    toggle->setChecked(true);
    toggle->setAutoRaise(true);
    mainWindow->statusBar()->addPermanentWidget(toggle);

    panels[title] = Panel {dock, toggle, title, 0};

    connect(toggle, &QToolButton::toggled, dock, [dock](bool on) { dock->setVisible(on); });
    connect(dock, &QDockWidget::visibilityChanged, this, [this, title](bool visible) {
        Panel& panel = panels[title];
        // visibilityChanged also fires when a dock is merely covered; the
        // toggle follows whether it is open.
        const bool open = !panel.dock->isHidden();
        if (panel.toggle->isChecked() != open) {
            QSignalBlocker block(panel.toggle);
            panel.toggle->setChecked(open);
        }
        if (visible && panel.alert) {
            panel.alert = 0;
            showAlert(panel);
        }
        if (!restoring) {
            saveLayout();
        }
    });
}

void ViewerShell::onContent(const QString& title, bool alert)
{
    auto it = panels.find(title);
    if (it == panels.end()) {
        return;
    }
    Panel& panel = it->second;
    if (!panel.dock->isHidden()) {
        return;  // nothing opens by itself, and an open panel needs no dot
    }
    const int level = alert ? 2 : 1;
    if (level > panel.alert) {
        panel.alert = level;
        showAlert(panel);
    }
}

void ViewerShell::showAlert(Panel& panel)
{
    if (panel.alert == 0) {
        panel.toggle->setText(panel.title);
        panel.toggle->setStyleSheet(QString());
        return;
    }
    panel.toggle->setText(QStringLiteral("● ") + panel.title);
    panel.toggle->setStyleSheet(panel.alert == 2 ? QStringLiteral("color: red;") : QString());
}

void ViewerShell::setViewOnly(bool on)
{
    if (on == viewOnly) {
        return;
    }
    viewOnly = on;
    if (viewOnlyAction->isChecked() != on) {
        QSignalBlocker block(viewOnlyAction);
        viewOnlyAction->setChecked(on);
    }
    if (on) {
        beforeViewOnly.clear();
        for (auto& [title, panel] : panels) {
            beforeViewOnly[title] = !panel.dock->isHidden();
            panel.dock->hide();
        }
    }
    else {
        for (auto& [title, panel] : panels) {
            panel.dock->setVisible(beforeViewOnly.contains(title) ? beforeViewOnly[title] : true);
        }
    }
}

json ViewerShell::panelState() const
{
    json out = json::object();
    for (const auto& [title, panel] : panels) {
        out[title.toStdString()] = {
            {"visible", !panel.dock->isHidden()},
            {"alert", panel.alert == 2 ? "error" : (panel.alert == 1 ? "new" : "none")},
            {"toggle", panel.toggle->text().toStdString()},
        };
    }
    return {{"panels", out}, {"viewOnly", viewOnly}};
}

void ViewerShell::saveLayout() const
{
    auto params = layoutParameters();
    params->SetASCII("State", mainWindow->saveState().toBase64().toStdString());
    for (const auto& [title, panel] : panels) {
        const bool shown = viewOnly ? (beforeViewOnly.contains(title) && beforeViewOnly.at(title))
                                    : !panel.dock->isHidden();
        params->SetBool((title.toStdString() + "Visible").c_str(), shown);
    }
}

void ViewerShell::restoreLayout()
{
    restoring = true;
    auto params = layoutParameters();
    const std::string state = params->GetASCII("State", "");
    if (!state.empty()) {
        mainWindow->restoreState(QByteArray::fromBase64(QByteArray::fromStdString(state)));
        // restoreState also brings back the upstream docks and toolbars that
        // were open when the state was saved in the classic GUI: hide them again.
        for (QToolBar* toolBar : mainWindow->findChildren<QToolBar*>()) {
            toolBar->hide();
        }
        for (QDockWidget* dock : mainWindow->findChildren<QDockWidget*>()) {
            if (!dock->objectName().startsWith(QStringLiteral("Agentic_"))) {
                dock->hide();
            }
        }
    }
    for (auto& [title, panel] : panels) {
        const bool shown = params->GetBool((title.toStdString() + "Visible").c_str(), true);
        panel.dock->setVisible(shown);
        panel.toggle->setChecked(shown);
    }
    restoring = false;
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

}  // namespace Agentic
