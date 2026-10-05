#include "qsockettextthread.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QNetworkProxy>

QSocketTextThread::QSocketTextThread(QObject *parent) :
    QThread(parent)
{
    connect(this, SIGNAL(finished()), this, SLOT(deleteLater()));
}

QSocketTextThread::~QSocketTextThread()
{
    qDebug() << "~QSocketTextThread";
}

void QSocketTextThread::run()
{
    const QString ip = qEnvironmentVariable("HOTEL_TEST_SERVER");
    const QString database = qEnvironmentVariable("HOTEL_TEST_DB_NAME");
    const QString user = qEnvironmentVariable("HOTEL_TEST_DB_USER");
    const QString password = qEnvironmentVariable("HOTEL_TEST_DB_PASSWORD");
    if (ip.isEmpty() || database.isEmpty() || user.isEmpty())
        return;

    auto payload = [&](const char *command, const QString &userName) {
        QJsonObject db;
        db.insert(QStringLiteral("database"), database);
        db.insert(QStringLiteral("user"), userName);
        db.insert(QStringLiteral("password"), password);
        QJsonObject commandObject;
        commandObject.insert(QStringLiteral("command"), QLatin1String(command));
        QJsonObject root;
        root.insert(QStringLiteral("db"), db);
        root.insert(QStringLiteral("command"), commandObject);
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    };

    QTcpSocket fSocket;
    fSocket.connectToHost(ip, 1250);
    if (fSocket.waitForConnected()) {
        const QByteArray data = payload("identify", QStringLiteral("Test main"));
        int size = data.size();
        QByteArray dataToSend;
        dataToSend.append(reinterpret_cast<const char*>(&size), sizeof(size));
        dataToSend.append(data);
        fSocket.write(dataToSend, dataToSend.length());
        fSocket.flush();
    } else {
        return;
    }
    QTcpSocket fSocketDraft;
    fSocketDraft.setProxy(QNetworkProxy::NoProxy);
    fSocketDraft.connectToHost(ip, 1250);
    if (fSocketDraft.waitForConnected()) {
        const QByteArray data = payload("draft", QStringLiteral("Test draft"));
        int size = data.size();
        QByteArray dataToSend;
        dataToSend.append(reinterpret_cast<const char*>(&size), sizeof(size));
        dataToSend.append(data);
        fSocketDraft.write(dataToSend, dataToSend.length());
        fSocketDraft.flush();
        fSocket.waitForReadyRead(20000);
        qDebug() << fSocket.readAll();
        qDebug() << fSocketDraft.readAll();
        for (int i = 0; i < 10; i++) {
            if (fSocket.waitForReadyRead(20000)) {
                qDebug() << fSocket.readAll();
                msleep(1000);
            } else {
                break;
            }
        }
        fSocket.disconnectFromHost();
        fSocketDraft.disconnectFromHost();
    }
}
