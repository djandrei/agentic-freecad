// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Base/Console.h>

#include "Protocol.h"
#include "StdioServer.h"

namespace Agentic
{

void StdinReader::run()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        Q_EMIT lineRead(QString::fromStdString(line));
    }
    Q_EMIT endOfInput();
}

StdioServer::StdioServer(Protocol& protocol, QObject* parent)
    : QObject(parent)
    , protocol(protocol)
{}

StdioServer::~StdioServer()
{
    if (reader && reader->isRunning()) {
        // The reader blocks in std::getline; there is no portable way to wake
        // it, and the process is shutting down anyway.
        reader->terminate();
        reader->wait();
    }
    delete reader;
}

void StdioServer::start()
{
    if (reader) {
        return;
    }
    reader = new StdinReader();
    connect(reader, &StdinReader::lineRead, this, &StdioServer::onLine, Qt::QueuedConnection);
    connect(reader, &StdinReader::endOfInput, this, &StdioServer::onEndOfInput, Qt::QueuedConnection);
    reader->start();
}

void StdioServer::setActivityListener(ActivityListener newListener)
{
    listener = std::move(newListener);
}

void StdioServer::onLine(const QString& line)
{
    const std::string request = line.toStdString();
    if (request.empty()) {
        return;
    }
    if (listener) {
        listener("> " + request);
    }
    const std::string response = protocol.handleLine(request);
    if (!response.empty()) {
        writeLine(response);
        if (listener) {
            listener("< " + response);
        }
    }
}

void StdioServer::onEndOfInput()
{
    Base::Console().log("Agentic: stdin closed, quitting\n");
    QCoreApplication::quit();
}

void StdioServer::writeLine(const std::string& line)
{
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace Agentic
