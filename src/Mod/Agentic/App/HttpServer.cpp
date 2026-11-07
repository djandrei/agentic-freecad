// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrl>

#include "HttpServer.h"
#include "Protocol.h"

namespace Agentic
{

namespace
{

constexpr qint64 maxRequestBytes = 16 * 1024 * 1024;

QString randomToken()
{
    QString token;
    for (int i = 0; i < 4; ++i) {
        token += QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, '0');
    }
    return token;
}

struct Head
{
    QByteArray method;
    QByteArray path;
    QHash<QByteArray, QByteArray> headers;  // lower-case names
};

Head parseHead(const QByteArray& head)
{
    Head parsed;
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) {
        return parsed;
    }
    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() >= 2) {
        parsed.method = requestLine[0];
        parsed.path = requestLine[1];
    }
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0) {
            parsed.headers.insert(line.left(colon).toLower(), line.mid(colon + 1).trimmed());
        }
    }
    return parsed;
}

bool originIsLocal(const QByteArray& origin)
{
    const QUrl url(QString::fromUtf8(origin));
    const QString host = url.host().toLower();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
        || host == QLatin1String("::1");
}

}  // namespace

HttpServer::HttpServer(Protocol& protocol, QObject* parent)
    : QObject(parent)
    , protocol(protocol)
    , tokenValue(randomToken())
{
    connect(&server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

bool HttpServer::listen(quint16 port)
{
    return server.listen(QHostAddress::LocalHost, port);
}

quint16 HttpServer::port() const
{
    return server.serverPort();
}

QString HttpServer::token() const
{
    return tokenValue;
}

QString HttpServer::url() const
{
    return QStringLiteral("http://127.0.0.1:%1/mcp").arg(server.serverPort());
}

void HttpServer::setActivityListener(ActivityListener newListener)
{
    listener = std::move(newListener);
}

void HttpServer::onNewConnection()
{
    while (QTcpSocket* socket = server.nextPendingConnection()) {
        buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, &HttpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &HttpServer::onDisconnected);
    }
}

void HttpServer::onDisconnected()
{
    auto socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        buffers.remove(socket);
        socket->deleteLater();
    }
}

void HttpServer::onReadyRead()
{
    auto socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }
    buffers[socket] += socket->readAll();
    if (buffers[socket].size() > maxRequestBytes) {
        respond(socket, 413, "Payload Too Large", "{}");
        return;
    }
    process(socket);
}

void HttpServer::process(QTcpSocket* socket)
{
    QByteArray& buffer = buffers[socket];
    const int headEnd = buffer.indexOf("\r\n\r\n");
    if (headEnd < 0) {
        return;  // wait for the complete head
    }
    const QByteArray head = buffer.left(headEnd);
    const Head parsed = parseHead(head);
    const qint64 contentLength = parsed.headers.value("content-length", "0").toLongLong();
    const int bodyStart = headEnd + 4;
    if (buffer.size() - bodyStart < contentLength) {
        return;  // wait for the complete body
    }
    const QByteArray body = buffer.mid(bodyStart, static_cast<int>(contentLength));
    buffer.remove(0, bodyStart + static_cast<int>(contentLength));
    handle(socket, head, body);
}

void HttpServer::handle(QTcpSocket* socket, const QByteArray& head, const QByteArray& body)
{
    const Head request = parseHead(head);

    if (request.path != "/mcp") {
        respond(socket, 404, "Not Found", R"({"error":"not found"})");
        return;
    }
    if (request.method != "POST") {
        respond(socket, 405, "Method Not Allowed", R"({"error":"POST only"})");
        return;
    }
    if (request.headers.contains("origin") && !originIsLocal(request.headers.value("origin"))) {
        respond(socket, 403, "Forbidden", R"({"error":"foreign origin"})");
        return;
    }
    const QByteArray expected = "Bearer " + tokenValue.toUtf8();
    if (request.headers.value("authorization") != expected) {
        respond(socket, 401, "Unauthorized", R"({"error":"bearer token required"})");
        return;
    }

    const std::string line = body.toStdString();
    if (listener) {
        listener("> " + line);
    }
    const std::string response = protocol.handleLine(line);
    if (response.empty()) {
        respond(socket, 202, "Accepted", QByteArray());
        return;
    }
    if (listener) {
        listener("< " + response);
    }
    respond(socket, 200, "OK", QByteArray::fromStdString(response));
}

void HttpServer::respond(
    QTcpSocket* socket,
    int status,
    const char* reason,
    const QByteArray& body,
    const char* contentType
)
{
    QByteArray out;
    out += "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    out += "Content-Type: ";
    out += contentType;
    out += "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Connection: close\r\n\r\n";
    out += body;
    socket->write(out);
    socket->disconnectFromHost();
}

}  // namespace Agentic
