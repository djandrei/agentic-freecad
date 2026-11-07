// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Base/Console.h>
#include <Base/Interpreter.h>

#include "DocumentTools.h"
#include "EngineSession.h"
#include "HttpServer.h"
#include "PreferenceTools.h"
#include "MessageLog.h"
#include "Protocol.h"
#include "Runtime.h"
#include "ScriptEngine.h"
#include "SessionManager.h"
#include "SessionTools.h"
#include "StderrLogger.h"
#include "StdioServer.h"
#include "Tools.h"

namespace Agentic
{

Runtime& Runtime::instance()
{
    // Never destroyed on purpose: the runtime holds a console observer and a
    // transport thread, and the order of static destruction against the
    // console singleton and Qt is not something to depend on at exit.
    static Runtime* runtime = new Runtime();
    return *runtime;
}

Runtime::Runtime()
    : cfg(AgentConfig::fromApplication())
    , messageLog(std::make_unique<MessageLog>())
    , sessionManager(std::make_unique<SessionManager>(observerList, *messageLog))
    , proto(std::make_unique<Protocol>())
{
    Base::Console().attachObserver(messageLog.get());

    registerCoreTools(*proto, *this);
    registerCoreResources(*proto, *this);
    registerCorePrompts(*proto);
    registerSessionTools(*proto, *this);
    registerDocumentTools(*proto, *this);
    registerPreferenceTools(*proto, *this);
    registerScriptTools(*proto, *this);

    proto->setToolHooks(
        [this](const std::string& name, const nlohmann::json&) {
            for (EngineSession* session : sessionManager->list()) {
                session->setCurrentTool(name);
            }
        },
        [this](const std::string& name, const nlohmann::json& args, double ms, bool ok) {
            observerList.each([&](SessionObserver& o) { o.toolCalled(name, args, ms, ok); });
        }
    );
}

Runtime::~Runtime() = default;

const AgentConfig& Runtime::config() const
{
    return cfg;
}

Protocol& Runtime::protocol()
{
    return *proto;
}

MessageLog& Runtime::messages()
{
    return *messageLog;
}

SessionManager& Runtime::sessions()
{
    return *sessionManager;
}

ObserverList& Runtime::observers()
{
    return observerList;
}

void Runtime::forwardActivity(const std::string& line)
{
    if (listener) {
        listener(line);
    }
}

bool Runtime::start()
{
    if (!cfg.enabled) {
        return false;
    }
    if (isRunning()) {
        return true;
    }

    if (!cfg.project.empty()) {
        try {
            sessionManager->open(cfg.project, true);
        }
        catch (const std::exception& e) {
            Base::Console().error("Agentic: cannot open project {}: {}\n", cfg.project, e.what());
        }
    }

    if (cfg.stdio) {
        logger = std::make_unique<StderrLogger>();
        Base::Console().attachObserver(logger.get());

        stdio = std::make_unique<StdioServer>(*proto);
        stdio->setActivityListener([this](const std::string& line) { forwardActivity(line); });
        stdio->start();
        Base::Console().log("Agentic: MCP server listening on stdin/stdout\n");
        return true;
    }

    auto server = std::make_unique<HttpServer>(*proto);
    if (!server->listen(static_cast<quint16>(cfg.port))) {
        Base::Console().error("Agentic: cannot listen on 127.0.0.1:{}\n", cfg.port);
        return false;
    }
    server->setActivityListener([this](const std::string& line) { forwardActivity(line); });
    http = std::move(server);

    // The client needs these lines; they go to stderr whatever the console
    // settings are, in a form that can be pasted.
    const std::string url = http->url().toStdString();
    const std::string token = http->token().toStdString();
    std::fprintf(stderr, "Agentic: MCP server at %s\n", url.c_str());
    std::fprintf(stderr, "Agentic: token %s\n", token.c_str());
    std::fprintf(
        stderr,
        "Agentic: claude mcp add --transport http freecad %s --header \"Authorization: Bearer "
        "%s\"\n",
        url.c_str(),
        token.c_str()
    );
    std::fflush(stderr);
    Base::Console().log("Agentic: MCP server at {}\n", url);
    return true;
}

bool Runtime::isRunning() const
{
    return stdio != nullptr || http != nullptr;
}

int Runtime::serve()
{
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance()) {
        static int argc = 1;
        static char name[] = "FreeCAD";
        static char* argv[] = {name, nullptr};
        app = std::make_unique<QCoreApplication>(argc, argv);
    }
    if (!start()) {
        return 1;
    }
    // Called from Python (the AgenticServe script): release the GIL while the
    // loop runs; tools that touch Python take it back.
    Base::PyGILStateRelease release;
    return QCoreApplication::exec();
}

std::string Runtime::httpUrl() const
{
    return http ? http->url().toStdString() : std::string();
}

std::string Runtime::httpToken() const
{
    return http ? http->token().toStdString() : std::string();
}

void Runtime::setGuiAvailable(bool available)
{
    gui = available;
}

bool Runtime::guiAvailable() const
{
    return gui;
}

void Runtime::setActivityListener(ActivityListener newListener)
{
    listener = std::move(newListener);
}

}  // namespace Agentic
