// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>
#include <CXX/Objects.hxx>

#include "PyBridge.h"

using json = nlohmann::json;

namespace Agentic
{

json callPython(const char* module, const char* function, const json& argument)
{
    Base::PyGILStateLocker lock;
    try {
        PyObject* imported = PyImport_ImportModule(module);
        if (!imported) {
            throw Py::Exception();
        }
        Py::Module mod(imported, true);
        Py::Callable fn(mod.getAttr(function));
        Py::Tuple args(1);
        args.setItem(0, Py::String(argument.dump()));
        Py::Object result = fn.apply(args);
        const std::string text = Py::String(result).as_std_string("utf-8");
        json parsed = json::parse(text, nullptr, false);
        if (parsed.is_discarded()) {
            throw std::runtime_error(std::string(module) + "." + function + " returned invalid JSON");
        }
        return parsed;
    }
    catch (Py::Exception&) {
        Base::PyException error;  // fetches and clears the Python error
        std::string message = error.what();
        const std::string& trace = error.getStackTrace();
        if (!trace.empty()) {
            message += "\n" + trace;
        }
        throw std::runtime_error(message);
    }
}

}  // namespace Agentic
