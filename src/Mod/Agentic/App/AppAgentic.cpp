// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>

#include "AgentConfig.h"
#include "Protocol.h"
#include "Runtime.h"

namespace Agentic
{

class Module: public Py::ExtensionModule<Module>
{
public:
    Module()
        : Py::ExtensionModule<Module>("Agentic")
    {
        add_varargs_method("version", &Module::version, "version() -> str: the FreeCAD version");
        add_varargs_method(
            "isAgentMode",
            &Module::isAgentMode,
            "isAgentMode() -> bool: whether FreeCAD was started with an --agent option"
        );
        add_varargs_method(
            "config",
            &Module::config,
            "config() -> dict: the agent-mode settings taken from the command line"
        );
        add_varargs_method(
            "handleRequest",
            &Module::handleRequest,
            "handleRequest(line: str) -> str: answer one JSON-RPC request as the MCP "
            "server would; returns '' for a notification"
        );
        initialize("The Agentic module: an MCP server that lets an agent drive FreeCAD.");
    }

private:
    Py::Object version(const Py::Tuple& args)
    {
        if (!PyArg_ParseTuple(args.ptr(), "")) {
            throw Py::Exception();
        }
        return Py::String(Protocol::serverVersion());
    }

    Py::Object isAgentMode(const Py::Tuple& args)
    {
        if (!PyArg_ParseTuple(args.ptr(), "")) {
            throw Py::Exception();
        }
        return Py::Boolean(Runtime::instance().config().enabled);
    }

    Py::Object config(const Py::Tuple& args)
    {
        if (!PyArg_ParseTuple(args.ptr(), "")) {
            throw Py::Exception();
        }
        const AgentConfig& cfg = Runtime::instance().config();
        Py::Dict dict;
        dict.setItem("enabled", Py::Boolean(cfg.enabled));
        dict.setItem("stdio", Py::Boolean(cfg.stdio));
        dict.setItem("headless", Py::Boolean(cfg.headless));
        dict.setItem("port", Py::Long(cfg.port));
        dict.setItem("project", Py::String(cfg.project));
        dict.setItem("gui", Py::Boolean(Runtime::instance().guiAvailable()));
        return dict;
    }

    Py::Object handleRequest(const Py::Tuple& args)
    {
        const char* line = nullptr;
        if (!PyArg_ParseTuple(args.ptr(), "s", &line)) {
            throw Py::Exception();
        }
        return Py::String(Runtime::instance().protocol().handleLine(line));
    }
};

PyObject* initModule()
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): ownership passes to the interpreter
    auto newModule = new Module;
    return Base::Interpreter().addModule(newModule);
}

}  // namespace Agentic

/* Python entry */
PyMOD_INIT_FUNC(Agentic)
{
    PyObject* mod = Agentic::initModule();
    Base::Console().log("Loading Agentic module... done\n");
    PyMOD_Return(mod);
}
