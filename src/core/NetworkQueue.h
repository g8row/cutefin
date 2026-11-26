#pragma once

#include <QByteArray>
#include <QList>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>

#include <atomic>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

// Owns the single QNetworkAccessManager used by the whole app (HTTP/2 +
// QNetworkDiskCache) and caps the number of in-flight requests so a
// scrolling grid of poster images can never starve the metadata calls a page
// is waiting on.
//
// Requests are queued at one of two priorities; all queued Metadata requests
// are dispatched before any queued Image request. Metadata is served in FIFO
// order, images in LIFO order so that the posters which just scrolled into view
// overtake the ones requested for content that has since left the screen.
//
// Callers bind a context QObject to each request: if the context is destroyed
// before the request is dispatched it is silently dropped, and if it is
// destroyed while the request is in flight the underlying QNetworkReply is
// aborted. The completion callback itself is connected with the context object
// as the receiver, so it is never invoked after the context is gone.
//
// Image requests bypass the QNetworkAccessManager disk cache entirely (both
// load and save): image consumers do their own caching.
class NetworkQueue : public QObject
{
    Q_OBJECT
public:
    enum class Priority {
        Metadata, // page content: item details, lists, login, playback info...
        Image,    // posters, backdrops, logos.
    };

    // Setting the flag (from any thread) abandons a request that is still
    // queued: it is discarded without being sent and its callback never runs.
    // It has no effect once the request is on the wire.
    using CancelToken = std::shared_ptr<std::atomic_bool>;

    static constexpr int kMaxInFlight = 6;

    explicit NetworkQueue(QObject *parent = nullptr);

    QNetworkAccessManager *networkAccessManager() const;

    // GET request. `context` may be nullptr for requests that should never
    // be auto-cancelled (e.g. login). `onDispatch`, if given, runs when the
    // request leaves the queue for the wire, possibly before get() returns.
    void get(const QNetworkRequest &request, Priority priority, QObject *context,
              const std::function<void(QNetworkReply *)> &callback, const CancelToken &cancelToken = {},
              const std::function<void()> &onDispatch = {});

    // POST request with a JSON (or empty) body.
    void post(const QNetworkRequest &request, const QByteArray &body, Priority priority, QObject *context,
               const std::function<void(QNetworkReply *)> &callback);

    // DELETE request. Jellyfin uses POST/DELETE on the same path as the
    // set/unset pair for user item state (played, favourite).
    void deleteResource(const QNetworkRequest &request, Priority priority, QObject *context,
                         const std::function<void(QNetworkReply *)> &callback);

private:
    struct QueuedRequest {
        QNetworkRequest request;
        QByteArray body;
        QByteArray verb;
        Priority priority = Priority::Metadata;
        bool hasContext = false;
        QPointer<QObject> context;
        std::function<void(QNetworkReply *)> callback;
        CancelToken cancelToken;
        std::function<void()> onDispatch;
    };

    void enqueue(QueuedRequest request, Priority priority);
    void scheduleDispatch();
    bool takeNext(QueuedRequest *out);
    void dispatch(const QueuedRequest &request);
    static bool isWanted(const QueuedRequest &request);

    QNetworkAccessManager *m_networkAccessManager;
    QList<QueuedRequest> m_metadataQueue; // served front first (FIFO)
    QList<QueuedRequest> m_imageQueue;    // served back first (LIFO)
    int m_activeCount = 0;
};
