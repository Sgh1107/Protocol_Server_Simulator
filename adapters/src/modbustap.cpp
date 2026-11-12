// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/adapters/modbustap.h"

#include <QDateTime>
#include <QHostAddress>
#include <QTcpSocket>

namespace protosim {

ModbusFrameTap::ModbusFrameTap(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &ModbusFrameTap::onNewConnection);
}

ModbusFrameTap::~ModbusFrameTap()
{
    stop();
}

Result<void> ModbusFrameTap::start(const Config &config)
{
    if (m_server.isListening())
        stop();

    m_config = config;

    const QHostAddress address(m_config.bindAddress);
    if (address.isNull()) {
        return Result<void>::failure(ErrorCode::InvalidArgument,
                                     QStringLiteral("监听地址无法解析"),
                                     m_config.bindAddress);
    }

    if (!m_server.listen(address, m_config.listenPort)) {
        return Result<void>::failure(ErrorCode::Io,
                                     QStringLiteral("Modbus TCP 端口监听失败"),
                                     QStringLiteral("%1:%2 — %3")
                                         .arg(m_config.bindAddress)
                                         .arg(m_config.listenPort)
                                         .arg(m_server.errorString()));
    }

    return {};
}

void ModbusFrameTap::stop()
{
    if (m_server.isListening())
        m_server.close();
    dropAllClients();
}

bool ModbusFrameTap::isListening() const
{
    return m_server.isListening();
}

quint16 ModbusFrameTap::listenPort() const
{
    return m_server.serverPort();
}

int ModbusFrameTap::activeClients() const
{
    return m_clients.size();
}

QStringList ModbusFrameTap::clientIds() const
{
    QStringList ids;
    ids.reserve(m_clients.size());
    for (const Client *client : m_clients)
        ids.append(client->id);
    return ids;
}

void ModbusFrameTap::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *front = m_server.nextPendingConnection();
        if (front == nullptr)
            break;

        const QString peer = QStringLiteral("%1:%2")
                                 .arg(front->peerAddress().toString())
                                 .arg(front->peerPort());

        // 连接数上限：直接掐掉第 N+1 条，不进入转发（AC-P0-13）
        if (m_clients.size() >= m_config.maxClients) {
            front->abort();
            front->deleteLater();
            Q_EMIT clientRejected(peer);
            continue;
        }

        auto *client = new Client;
        client->id = peer;
        client->front = front;

        auto *back = new QTcpSocket(this);
        client->back = back;

        m_clients.insert(front, client);

        connect(front, &QTcpSocket::readyRead, this, [this, client] { pumpInbound(client); });
        connect(front, &QTcpSocket::disconnected, this, [this, client] {
            dropClient(client, true);
        });
        connect(back, &QTcpSocket::readyRead, this, [this, client] { pumpOutbound(client); });
        connect(back, &QTcpSocket::disconnected, this, [this, client] {
            dropClient(client, true);
        });

        back->connectToHost(QHostAddress::LocalHost, m_config.backendPort);
        // 先写后连：QTcpSocket 会把数据缓存到连接建立后再发，不需要额外等待

        Q_EMIT clientConnected(client->id);
    }
}

void ModbusFrameTap::pumpInbound(Client *client)
{
    if (client == nullptr || client->front == nullptr)
        return;

    const QByteArray chunk = client->front->readAll();
    if (chunk.isEmpty())
        return;

    if (client->back != nullptr && client->back->state() != QAbstractSocket::UnconnectedState)
        client->back->write(chunk);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QVector<QByteArray> frames = client->inbound.consume(chunk);
    for (const QByteArray &adu : frames)
        Q_EMIT frameArrived(adu, client->id, true, now);
}

void ModbusFrameTap::pumpOutbound(Client *client)
{
    if (client == nullptr || client->back == nullptr)
        return;

    const QByteArray chunk = client->back->readAll();
    if (chunk.isEmpty())
        return;

    if (client->front != nullptr)
        client->front->write(chunk);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QVector<QByteArray> frames = client->outbound.consume(chunk);
    for (const QByteArray &adu : frames)
        Q_EMIT frameArrived(adu, client->id, false, now);
}

void ModbusFrameTap::dropClient(Client *client, bool notify)
{
    if (client == nullptr)
        return;

    QTcpSocket *front = client->front;
    QTcpSocket *back = client->back;

    if (front != nullptr) {
        m_clients.remove(front);
        front->disconnect(this);
    }
    if (back != nullptr)
        back->disconnect(this);

    const QString id = client->id;
    delete client;

    if (front != nullptr) {
        front->abort();
        front->deleteLater();
    }
    if (back != nullptr) {
        back->abort();
        back->deleteLater();
    }

    if (notify)
        Q_EMIT clientDisconnected(id);
}

void ModbusFrameTap::dropAllClients()
{
    const QList<Client *> clients = m_clients.values();
    for (Client *client : clients)
        dropClient(client, false);
    m_clients.clear();
}

} // namespace protosim
