/****************************************************************************
**
** Copyright (C) 2020 Prashanth N Udupa
** Author: Prashanth N Udupa (prashanth@scrite.io,
**                            prashanth.udupa@gmail.com,
**                            prashanth@vcreatelogic.com)
**
** This code is distributed under GPL v3. Complete text of the license
** can be found here: https://www.gnu.org/licenses/gpl-3.0.txt
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/

#include "callgraph.h"
#include "garbagecollector.h"
#include "networkaccessmanager.h"
#include "application.h"

#include <QNetworkReply>
#include <QCoreApplication>
#include <QTimer>

namespace {

class OfflineNetworkReply final : public QNetworkReply
{
public:
    OfflineNetworkReply(QNetworkAccessManager::Operation operation,
                        const QNetworkRequest &request, QObject *parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setOpenMode(QIODevice::ReadOnly);
        setError(QNetworkReply::OperationCanceledError,
                 QStringLiteral("Offline mode is enabled; no network request was made."));
        QTimer::singleShot(0, this, [this]() {
            if (isFinished())
                return;
            setFinished(true);
            emit errorOccurred(error());
            emit finished();
        });
    }

    void abort() override
    {
        if (isFinished())
            return;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Request canceled."));
        setFinished(true);
        emit errorOccurred(error());
        emit finished();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        Q_UNUSED(data)
        Q_UNUSED(maxSize)
        return -1;
    }
};

}

NetworkAccessManager *NetworkAccessManager::INSTANCE = nullptr;

NetworkAccessManager *NetworkAccessManager::instance()
{
    // CAPTURE_FIRST_CALL_GRAPH;
    if (NetworkAccessManager::INSTANCE)
        return NetworkAccessManager::INSTANCE;

    return new NetworkAccessManager(qApp);
}

NetworkAccessManager::NetworkAccessManager(QObject *parent) : QNetworkAccessManager(parent)
{
    // CAPTURE_CALL_GRAPH;
    NetworkAccessManager::INSTANCE = this;
    connect(this, &QNetworkAccessManager::finished, this, &NetworkAccessManager::onReplyFinished);
    connect(this, &QNetworkAccessManager::sslErrors, this, &NetworkAccessManager::onSslErrors);
    connect(Application::instance(), &Application::offlineModeChanged, this, [this]() {
        if (!Application::instance()->offlineMode())
            return;
        const QList<QNetworkReply *> replies = m_replies;
        for (QNetworkReply *reply : replies) {
            if (reply && !reply->isFinished())
                reply->abort();
        }
    });
}

NetworkAccessManager::~NetworkAccessManager()
{
    NetworkAccessManager::INSTANCE = nullptr;
}

QNetworkReply *NetworkAccessManager::createRequest(QNetworkAccessManager::Operation op,
                                                   const QNetworkRequest &request,
                                                   QIODevice *outgoingData)
{
    if (Application::instance() && Application::instance()->offlineMode()) {
        auto *reply = new OfflineNetworkReply(op, request, this);
        m_replies.append(reply);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply]() { this->onReplyFinished(reply); });
        return reply;
    }

    QNetworkReply *reply = QNetworkAccessManager::createRequest(op, request, outgoingData);
    if (reply)
        m_replies.append(reply);

    return reply;
}

void NetworkAccessManager::onReplyFinished(QNetworkReply *reply)
{
    if (reply != nullptr && m_replies.removeOne(reply)) {
        GarbageCollector::instance()->add(reply);

        if (m_replies.isEmpty()) {
            NetworkAccessManager::INSTANCE = nullptr;
            GarbageCollector::instance()->add(this);
        }
    }
}

void NetworkAccessManager::onSslErrors(QNetworkReply *reply, const QList<QSslError> &errors)
{
    reply->ignoreSslErrors(errors);
}
