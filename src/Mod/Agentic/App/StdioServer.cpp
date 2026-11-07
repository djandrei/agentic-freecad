// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#ifdef _WIN32
# include <io.h>
# define dup _dup
# define dup2 _dup2
# define fileno _fileno
# define fdopen _fdopen
#else
# include <unistd.h>
#endif

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
    claimStdout();
    reader = new StdinReader();
    connect(reader, &StdinReader::lineRead, this, &StdioServer::onLine, Qt::QueuedConnection);
    connect(reader, &StdinReader::endOfInput, this, &StdioServer::onEndOfInput, Qt::QueuedConnection);
    reader->start();
}

void StdioServer::onLine(const QString& line)
{
    const std::string request = line.toStdString();
    if (request.empty()) {
        return;
    }
    const std::string response = protocol.handleLine(request);
    if (!response.empty()) {
        writeLine(response);
    }
}

void StdioServer::onEndOfInput()
{
    Base::Console().log("Agentic: stdin closed, quitting\n");
    QCoreApplication::quit();
}

FILE* StdioServer::transport = nullptr;

void StdioServer::claimStdout()
{
    // The transport gets a private duplicate of stdout and descriptor 1 is
    // pointed at stderr, so nothing else in the process (a Python print in
    // the console binary, a C library writing to stdout, an addon) can put a
    // stray line between two JSON-RPC messages.
    std::fflush(stdout);
    const int out = dup(fileno(stdout));
    if (out < 0) {
        transport = stdout;
        return;
    }
    dup2(fileno(stderr), fileno(stdout));
    transport = fdopen(out, "w");
    if (!transport) {
        transport = stdout;
    }
}

void StdioServer::writeLine(const std::string& line)
{
    FILE* target = transport ? transport : stdout;
    std::fwrite(line.data(), 1, line.size(), target);
    std::fputc('\n', target);
    std::fflush(target);
}

}  // namespace Agentic
