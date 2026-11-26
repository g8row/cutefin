#include "app/ImageProviders.h"

#include "core/BlurHash.h"
#include "core/ImageCache.h"
#include "player/TrickplayCache.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QQuickTextureFactory>
#include <QStringList>
#include <QUrl>

#include <atomic>
#include <memory>

namespace {

constexpr int kBlurHashCacheEntries = 256;
constexpr int kBlurHashSize = 16;

QString percentDecoded(const QString &text)
{
    return QUrl::fromPercentEncoding(text.toUtf8());
}

// Response for one image://jellyfin/ request.
//
// The response lives on Qt Quick's image reader thread while ImageCache
// answers on its own (GUI) thread, and Qt may cancel and delete the response
// at any point in between. Both sides therefore meet only in a shared,
// mutex-guarded State that outlives whichever finishes last, and completion is
// always queued back to the response's thread: m_image and finished() are
// touched there only, never under the lock. The cache outlives every response.
class JellyfinImageResponse : public QQuickImageResponse
{
public:
    struct State {
        QMutex mutex;
        JellyfinImageResponse *response = nullptr; // null once destroyed
        bool done = false;                         // finished() already emitted
        const ImageCache::CancelToken cancelToken = std::make_shared<std::atomic_bool>(false);
    };

    explicit JellyfinImageResponse(ImageCache *cache)
        : m_cache(cache)
        , m_state(std::make_shared<State>())
    {
        m_state->response = this;
    }

    ~JellyfinImageResponse() override
    {
        QMutexLocker locker(&m_state->mutex);
        m_state->response = nullptr;
    }

    std::shared_ptr<State> state() const { return m_state; }

    // Safe from any thread; only the first completion counts.
    static void complete(const std::shared_ptr<State> &state, const QImage &image)
    {
        // Holding the lock keeps the response alive while posting to it; the
        // event is discarded if the response is destroyed before delivery.
        QMutexLocker locker(&state->mutex);
        JellyfinImageResponse *response = state->response;
        if (!response || state->done)
            return;
        state->done = true;
        QMetaObject::invokeMethod(
            response,
            [response, image]() {
                response->m_image = image;
                emit response->finished();
            },
            Qt::QueuedConnection);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return m_image.isNull() ? nullptr : QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("Image unavailable") : QString();
    }

    void cancel() override
    {
        m_state->cancelToken->store(true);
        if (ImageCache *cache = m_cache) {
            // Lets the cache drop queued work that nobody else is waiting for.
            QMetaObject::invokeMethod(
                cache, [cache]() { cache->discardCancelledRequests(); }, Qt::QueuedConnection);
        }
        complete(m_state, QImage()); // Qt still expects finished()
    }

private:
    ImageCache *const m_cache;
    std::shared_ptr<State> m_state;
    QImage m_image;
};

} // namespace

JellyfinImageProvider::JellyfinImageProvider(ImageCache *imageCache)
    : m_imageCache(imageCache)
{
}

QQuickImageResponse *JellyfinImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    auto *response = new JellyfinImageResponse(m_imageCache);
    const std::shared_ptr<JellyfinImageResponse::State> state = response->state();

    QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() < 3)
        parts = percentDecoded(id).split(QLatin1Char('/')); // whole id was encoded
    else
        for (QString &part : parts)
            part = percentDecoded(part);

    const QString itemId = parts.value(0);
    bool typeOk = false;
    const ImageCache::ImageType type = ImageCache::imageTypeFromString(parts.value(1), &typeOk);
    const QString tag = parts.value(2);
    const int maxWidth = requestedSize.width() > 0 ? requestedSize.width() : parts.value(3).toInt();

    ImageCache *cache = m_imageCache;
    if (!cache || itemId.isEmpty() || !typeOk || tag.isEmpty()) {
        JellyfinImageResponse::complete(state, QImage());
        return response;
    }

    // ImageCache is called on its own thread. No context object is needed: the
    // response's cancel token stops the callback and any work still queued.
    QMetaObject::invokeMethod(
        cache,
        [cache, state, itemId, type, tag, maxWidth]() {
            cache->requestImage(
                itemId, type, tag, maxWidth, nullptr,
                [state](const QImage &image) { JellyfinImageResponse::complete(state, image); }, state->cancelToken);
        },
        Qt::QueuedConnection);

    return response;
}

BlurHashImageProvider::BlurHashImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_cache(kBlurHashCacheEntries)
{
}

QImage BlurHashImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize) // the GPU upscales the tiny decode

    const QString hash = percentDecoded(id);

    QImage image;
    {
        QMutexLocker locker(&m_mutex);
        if (const QImage *hit = m_cache.object(hash))
            image = *hit;
    }

    if (image.isNull()) {
        image = BlurHash::decode(hash, kBlurHashSize, kBlurHashSize);
        if (!image.isNull()) {
            QMutexLocker locker(&m_mutex);
            m_cache.insert(hash, new QImage(image));
        }
    }

    if (size)
        *size = image.size();
    return image;
}

TrickplayImageProvider::TrickplayImageProvider(TrickplayCache *trickplayCache)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_trickplayCache(trickplayCache)
{
}

QImage TrickplayImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // The revision segment only exists to make QML re-request the URL.
    bool ok = false;
    const qint64 positionMs = id.section(QLatin1Char('/'), 0, 0).toLongLong(&ok);

    QImage image;
    if (ok && m_trickplayCache)
        image = m_trickplayCache->thumbnailIfCached(positionMs);

    if (size)
        *size = image.size();
    if (!image.isNull() && (requestedSize.width() > 0 || requestedSize.height() > 0)) {
        const QSize bounds(requestedSize.width() > 0 ? requestedSize.width() : image.width(),
                           requestedSize.height() > 0 ? requestedSize.height() : image.height());
        image = image.scaled(bounds, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}
