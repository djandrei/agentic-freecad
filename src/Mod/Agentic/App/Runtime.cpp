// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Base/Console.h>

#include "Protocol.h"
#include "Runtime.h"
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
    , proto(std::make_unique<Protocol>())
{
    registerCoreTools(*proto, *this);
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

bool Runtime::start()
{
    if (!cfg.enabled) {
        return false;
    }
    if (isRunning()) {
        return true;
    }

    if (cfg.stdio) {
        logger = std::make_unique<StderrLogger>();
        Base::Console().attachObserver(logger.get());

        stdio = std::make_unique<StdioServer>(*proto);
        stdio->setActivityListener([this](const std::string& line) {
            if (listener) {
                listener(line);
            }
        });
        stdio->start();
        Base::Console().log("Agentic: MCP server listening on stdin/stdout\n");
        return true;
    }

    Base::Console().warning(
        "Agentic: the HTTP transport is not implemented yet; start with --agent-stdio\n"
    );
    return false;
}

bool Runtime::isRunning() const
{
    return stdio != nullptr;
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
