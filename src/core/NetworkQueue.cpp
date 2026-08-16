#include "NetworkQueue.h"

#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QStandardPaths>

NetworkQueue::NetworkQueue(QObject *parent)
    : QObject(parent)
    , m_networkAccessManager(new QNetworkAccessManager(this))
{
    auto *diskCache = new QNetworkDiskCache(m_networkAccessManager);
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/network");
    diskCache->setCacheDirectory(cacheDir);
    diskCache->setMaximumCacheSize(50LL * 1024 * 1024);
    m_networkAccessManager->setCache(diskCache);
}

QNetworkAccessManager *NetworkQueue::networkAccessManager() const
{
    return m_networkAccessManager;
}

void NetworkQueue::get(const QNetworkRequest &request, Priority priority, QObject *context,
                        const std::function<void(QNetworkReply *)> &callback)
{
    QueuedRequest qr;
    qr.request = request;
    qr.verb = "GET";
    qr.hasContext = context != nullptr;
    qr.context = context;
    qr.callback = callback;
    enqueue(std::move(qr), priority);
}

void NetworkQueue::post(const QNetworkRequest &request, const QByteArray &body, Priority priority, QObject *context,
                         const std::function<void(QNetworkReply *)> &callback)
{
    QueuedRequest qr;
    qr.request = request;
    qr.body = body;
    qr.verb = "POST";
    qr.hasContext = context != nullptr;
    qr.context = context;
    qr.callback = callback;
    enqueue(std::move(qr), priority);
}

void NetworkQueue::deleteResource(const QNetworkRequest &request, Priority priority, QObject *context,
                                   const std::function<void(QNetworkReply *)> &callback)
{
    QueuedRequest qr;
    qr.request = request;
    qr.verb = "DELETE";
    qr.hasContext = context != nullptr;
    qr.context = context;
    qr.callback = callback;
    enqueue(std::move(qr), priority);
}

void NetworkQueue::enqueue(QueuedRequest request, Priority priority)
{
    QNetworkRequest req = request.request;
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    request.request = req;

    if (priority == Priority::Metadata)
        m_metadataQueue.enqueue(std::move(request));
    else
        m_imageQueue.enqueue(std::move(request));

    scheduleDispatch();
}

bool NetworkQueue::isContextAlive(const QueuedRequest &request)
{
    return !request.hasContext || !request.context.isNull();
}

bool NetworkQueue::takeNext(QueuedRequest *out)
{
    for (QQueue<QueuedRequest> *queue : {&m_metadataQueue, &m_imageQueue}) {
        while (!queue->isEmpty()) {
            QueuedRequest qr = queue->dequeue();
            if (!isContextAlive(qr))
                continue; // dropped: the requesting widget is already gone.
            *out = std::move(qr);
            return true;
        }
    }
    return false;
}

void NetworkQueue::scheduleDispatch()
{
    while (m_activeCount < kMaxInFlight) {
        QueuedRequest next;
        if (!takeNext(&next))
            break;
        dispatch(next);
    }
}

void NetworkQueue::dispatch(const QueuedRequest &request)
{
    QNetworkReply *reply = nullptr;
    if (request.verb == "POST") {
        QNetworkRequest req = request.request;
        if (!req.header(QNetworkRequest::ContentTypeHeader).isValid())
            req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        reply = m_networkAccessManager->post(req, request.body);
    } else if (request.verb == "DELETE") {
        reply = m_networkAccessManager->deleteResource(request.request);
    } else {
        reply = m_networkAccessManager->get(request.request);
    }

    ++m_activeCount;

    // Always fires, regardless of context lifetime, to keep the concurrency
    // accounting correct and free the reply.
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        --m_activeCount;
        reply->deleteLater();
        scheduleDispatch();
    });

    if (request.hasContext) {
        // Abort in-flight requests whose requester has already died.
        connect(request.context, &QObject::destroyed, reply, &QNetworkReply::abort);
    }

    QObject *receiver = request.hasContext ? request.context.data() : this;
    const std::function<void(QNetworkReply *)> callback = request.callback;
    connect(reply, &QNetworkReply::finished, receiver, [callback, reply]() {
        if (callback)
            callback(reply);
    });
}
