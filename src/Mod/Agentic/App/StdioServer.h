// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdio>
#include <functional>
#include <string>

#include <QObject>
#include <QThread>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

class Protocol;

/// Reads newline-delimited input from stdin on its own thread.
class StdinReader: public QThread
{
    Q_OBJECT

public:
    using QThread::QThread;

Q_SIGNALS:
    void lineRead(const QString& line);
    void endOfInput();

protected:
    void run() override;
};

/// The stdio transport: one JSON-RPC message per line on stdin and stdout.
///
/// Lines are read on a worker thread and handed to the main thread through a
/// queued signal, so every request is executed on the thread that owns the
/// documents and the Python interpreter, one at a time. The server keeps a
/// private duplicate of stdout for its responses and points descriptor 1 at
/// stderr, so no other output in the process can corrupt the transport.
class AgenticExport StdioServer: public QObject
{
    Q_OBJECT

public:
    explicit StdioServer(Protocol& protocol, QObject* parent = nullptr);
    ~StdioServer() override;

    void start();

private Q_SLOTS:
    void onLine(const QString& line);
    void onEndOfInput();

private:
    static void claimStdout();
    static void writeLine(const std::string& line);

    static FILE* transport;

    Protocol& protocol;
    StdinReader* reader {nullptr};
};

}  // namespace Agentic
