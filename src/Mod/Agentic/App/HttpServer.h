// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <functional>
#include <string>

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTcpServer>

#include <Mod/Agentic/AgenticGlobal.h>

class QTcpSocket;

namespace Agentic
{

class Protocol;

/// The HTTP transport: MCP Streamable HTTP without the SSE stream.
///
/// Listens on 127.0.0.1 only. Every request is a POST to /mcp carrying one
/// JSON-RPC message; the response carries the JSON-RPC response, or 202 for
/// a notification. A per-start bearer token is required, and a request with
/// an Origin header that is not local is refused. Qt sockets deliver on the
/// thread that owns the server, the main thread, so requests already run
/// where the documents and the interpreter live.
class AgenticExport HttpServer: public QObject
{
    Q_OBJECT

public:
    explicit HttpServer(Protocol& protocol, QObject* parent = nullptr);

    bool listen(quint16 port);
    quint16 port() const;
    QString token() const;
    QString url() const;


private Q_SLOTS:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void process(QTcpSocket* socket);
    void handle(QTcpSocket* socket, const QByteArray& head, const QByteArray& body);
    static void respond(
        QTcpSocket* socket,
        int status,
        const char* reason,
        const QByteArray& body,
        const char* contentType = "application/json"
    );

    Protocol& protocol;
    QTcpServer server;
    QString tokenValue;
    QHash<QTcpSocket*, QByteArray> buffers;
};

}  // namespace Agentic
