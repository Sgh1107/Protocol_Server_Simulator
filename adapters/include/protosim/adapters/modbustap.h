// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/adapters/modbusframe.h"
#include "protosim/result.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTcpServer>

class QTcpSocket;

namespace protosim {

/// Modbus TCP 的**帧观察层**。
///
/// 为什么需要它：Qt 的 QModbusTcpServer 不暴露原始 ADU，也不告诉调用方
/// 某个请求来自哪条连接。而本项目要求报文日志里有真实 hex（含事务号）
/// 和客户端归属，审计里也要记谁写的 —— 这两样只能在这一层拿到。
///
/// 它不做协议解析（解析在 QModbusTcpServer 里），只做三件事：
///   1. 监听对外端口，按连接数上限接收/拒绝客户端；
///   2. 每条连接开一条到内部 QModbusTcpServer 的回环连接，字节原样双向透传；
///   3. 按 MBAP 长度切出完整 ADU，逐帧上报（收/发两个方向）。
///
/// 因此主站看到的行为与直连 QModbusTcpServer 完全一致，事务号也不会被改写。
class ModbusFrameTap : public QObject
{
    Q_OBJECT

public:
    struct Config
    {
        QString bindAddress = QStringLiteral("127.0.0.1");
        quint16 listenPort = 1502;
        quint16 backendPort = 1502; // 内部 QModbusTcpServer 的端口，不对局域网暴露
        int maxClients = 8;
    };

    explicit ModbusFrameTap(QObject *parent = nullptr);
    ~ModbusFrameTap() override;

    Result<void> start(const Config &config);
    void stop();

    bool isListening() const;
    quint16 listenPort() const;
    int activeClients() const;
    QStringList clientIds() const;

Q_SIGNALS:
    /// inbound=true 表示主站发来的请求，false 表示模拟器发出的响应。
    void frameArrived(const QByteArray &adu, const QString &clientId, bool inbound,
                      qint64 timestampMs);
    void clientConnected(const QString &clientId);
    void clientDisconnected(const QString &clientId);
    void clientRejected(const QString &peer);

private:
    struct Client
    {
        QString id;
        QTcpSocket *front = nullptr;
        QTcpSocket *back = nullptr;
        modbus::AduReassembler inbound;
        modbus::AduReassembler outbound;
    };

    void onNewConnection();
    void pumpInbound(Client *client);
    void pumpOutbound(Client *client);
    void dropClient(Client *client, bool notify);
    void dropAllClients();

    QTcpServer m_server;
    Config m_config;
    QHash<QTcpSocket *, Client *> m_clients; // key = front socket
    int m_clientSequence = 0;
};

} // namespace protosim
