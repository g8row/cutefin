#include "NetworkQueue.h"

#include <QBuffer>
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
                        const std::function<void(QNetworkReply *)> &callback, const CancelToken &cancelToken,
                        const std::function<void()> &onDispatch)
{
    QueuedRequest qr;
    qr.request = request;
    qr.verb = "GET";
    qr.hasContext = context != nullptr;
    qr.context = context;
    qr.callback = callback;
    qr.cancelToken = cancelToken;
    qr.onDispatch = onDispatch;
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
    request.priority = priority;
    request.request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

    if (priority == Priority::Metadata)
        m_metadataQueue.append(std::move(request));
    else
        m_imageQueue.append(std::move(request));

    scheduleDispatch();
}

bool NetworkQueue::isWanted(const QueuedRequest &request)
{
    if (request.hasContext && request.context.isNull())
        return false;
    return !request.cancelToken || !request.cancelToken->load();
}

bool NetworkQueue::takeNext(QueuedRequest *out)
{
    // Unwanted entries (requester gone, or cancelled) are dropped unsent.
    while (!m_metadataQueue.isEmpty()) {
        QueuedRequest qr = m_metadataQueue.takeFirst();
        if (!isWanted(qr))
            continue;
        *out = std::move(qr);
        return true;
    }
    while (!m_imageQueue.isEmpty()) {
        QueuedRequest qr = m_imageQueue.takeLast();
        if (!isWanted(qr))
            continue;
        *out = std::move(qr);
        return true;
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
    QNetworkRequest req = request.request;
    if (request.priority == Priority::Image) {
        // Keeps image traffic out of QNetworkDiskCache, which would store a
        // second copy and run its expiry scans on this (the GUI) thread.
        req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    }

    QNetworkReply *reply = nullptr;
    if (request.verb == "POST") {
        if (!req.header(QNetworkRequest::ContentTypeHeader).isValid())
            req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        if (request.body.isEmpty()) {
            reply = m_networkAccessManager->post(req, QByteArray());
        } else {
            // Parent to NetworkQueue (created after m_networkAccessManager) and
            // defer deletion past QNetworkReply::~QNetworkReply so Qt's HTTP/2
            // worker thread never sees the upload QIODevice destroyed mid-upload
            // during aborts or shutdown teardown.
            auto *buffer = new QBuffer(this);
            buffer->setData(request.body);
            buffer->open(QIODevice::ReadOnly);
            req.setHeader(QNetworkRequest::ContentLengthHeader, request.body.size());
            reply = m_networkAccessManager->post(req, buffer);
            connect(reply, &QObject::destroyed, buffer, &QObject::deleteLater);
        }
    } else if (request.verb == "DELETE") {
        reply = m_networkAccessManager->deleteResource(req);
    } else {
        reply = m_networkAccessManager->get(req);
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
    const bool hasContext = request.hasContext;
    const QPointer<QObject> guard = request.context;
    const std::function<void(QNetworkReply *)> callback = request.callback;
    connect(reply, &QNetworkReply::finished, receiver, [hasContext, guard, callback, reply]() {
        // abort() above finishes the reply while the context is mid-destruction.
        if (hasContext && guard.isNull())
            return;
        if (callback)
            callback(reply);
    });

    if (request.onDispatch)
        request.onDispatch();
}
