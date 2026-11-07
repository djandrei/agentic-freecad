// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>
#include <Gui/MainWindow.h>

#include <Mod/Agentic/App/Runtime.h>

#include "ViewerShell.h"

namespace AgenticGui
{

class Module: public Py::ExtensionModule<Module>
{
public:
    Module()
        : Py::ExtensionModule<Module>("AgenticGui")
    {
        initialize("The GUI part of the Agentic module: the agent viewer.");
    }
};

PyObject* initModule()
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): ownership passes to the interpreter
    auto newModule = new Module;
    return Base::Interpreter().addModule(newModule);
}

void installAgentMode()
{
    Agentic::Runtime& runtime = Agentic::Runtime::instance();
    runtime.setGuiAvailable(true);
    if (!runtime.config().enabled) {
        return;
    }

    Gui::MainWindow* mw = Gui::getMainWindow();
    auto shell = new Agentic::ViewerShell(mw);
    runtime.setActivityListener([shell](const std::string& line) { shell->appendActivity(line); });

    // This runs during InitGui.py, before the event loop. A zero-timer fires as
    // soon as the loop starts, after the workbench, toolbars and window settings
    // are in place. MainWindow::guiInitialized is not used: it is skipped for a
    // hidden window and the module must not depend on how the window is shown.
    QTimer::singleShot(0, shell, [shell, &runtime] {
        shell->apply();
        runtime.start();
    });
}

}  // namespace AgenticGui

/* Python entry */
PyMOD_INIT_FUNC(AgenticGui)
{
    Base::Console().log("Loading GUI of Agentic module... ");
    PyObject* mod = AgenticGui::initModule();
    AgenticGui::installAgentMode();
    Base::Console().log("done\n");
    PyMOD_Return(mod);
}
