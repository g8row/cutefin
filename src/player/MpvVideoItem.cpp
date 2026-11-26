#include "player/MpvVideoItem.h"

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickOpenGLUtils>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtMath>

#include <memory>
#include <utility>

#ifdef CUTEFIN_ENABLE_MPV
#include <mpv/client.h>
#include <mpv/render_gl.h>

#if defined(Q_OS_LINUX)
#include <QGuiApplication>
#endif
#endif

namespace {

Q_LOGGING_CATEGORY(lcMpv, "cutefin.mpv")

#ifdef CUTEFIN_ENABLE_MPV
constexpr int kMaxRecentLogs = 40;
// QML bindings on `position` (seek bar, time labels, skip/up-next logic) do
// not need 60 Hz churn; four updates a second is visually indistinguishable
// for a seek bar and saves real CPU on dual-core machines.
constexpr double kPositionEmitDelta = 0.25;
// Same idea for the cache bar.
constexpr double kCacheEmitDelta = 0.5;
#endif

const QString kDash = QStringLiteral("—");

// Clears the FBO to black: used both by the stub renderer (no libmpv) and by
// the real renderer before mpv has produced its first frame.
void clearToBlack()
{
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context)
        return;
    QOpenGLFunctions *gl = context->functions();
    gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
}

class StubRenderer : public QQuickFramebufferObject::Renderer
{
public:
    void render() override
    {
        clearToBlack();
        QQuickOpenGLUtils::resetOpenGLState();
    }
};

QString formatBitrate(double bitsPerSecond)
{
    if (bitsPerSecond <= 0.0)
        return kDash;
    if (bitsPerSecond >= 1'000'000.0)
        return QStringLiteral("%1 Mbps").arg(bitsPerSecond / 1'000'000.0, 0, 'f', 2);
    return QStringLiteral("%1 kbps").arg(bitsPerSecond / 1000.0, 0, 'f', 0);
}

QString formatBytesPerSecond(double bytesPerSecond)
{
    if (bytesPerSecond <= 0.0)
        return kDash;
    if (bytesPerSecond >= 1024.0 * 1024.0)
        return QStringLiteral("%1 MiB/s").arg(bytesPerSecond / (1024.0 * 1024.0), 0, 'f', 2);
    return QStringLiteral("%1 KiB/s").arg(bytesPerSecond / 1024.0, 0, 'f', 0);
}

QString orDash(const QString &value)
{
    return value.isEmpty() ? kDash : value;
}

QString trackLabel(const MpvVideoItem::Track &track)
{
    QString label;
    if (!track.title.isEmpty() && !track.lang.isEmpty())
        label = QStringLiteral("%1 [%2]").arg(track.title, track.lang);
    else if (!track.title.isEmpty())
        label = track.title;
    else if (!track.lang.isEmpty())
        label = track.lang;
    else
        label = QStringLiteral("Track %1").arg(track.id);

    QStringList flags;
    if (!track.codec.isEmpty())
        flags << track.codec;
    if (track.forced)
        flags << QStringLiteral("Forced");
    if (track.hearingImpaired)
        flags << QStringLiteral("SDH");
    if (track.external)
        flags << QStringLiteral("External");
    if (!flags.isEmpty())
        label += QStringLiteral(" (%1)").arg(flags.join(QStringLiteral(", ")));
    return label;
}

[[maybe_unused]] QVariantMap trackToVariant(const MpvVideoItem::Track &track)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), track.id);
    map.insert(QStringLiteral("type"), track.type);
    map.insert(QStringLiteral("lang"), track.lang);
    map.insert(QStringLiteral("title"), track.title);
    map.insert(QStringLiteral("codec"), track.codec);
    map.insert(QStringLiteral("selected"), track.selected);
    map.insert(QStringLiteral("forced"), track.forced);
    map.insert(QStringLiteral("hearingImpaired"), track.hearingImpaired);
    map.insert(QStringLiteral("external"), track.external);
    map.insert(QStringLiteral("label"), trackLabel(track));
    return map;
}

#ifdef CUTEFIN_ENABLE_MPV

// Selected-track properties ("aid", "sid", "secondary-sid") read back as a
// track id, or "no"/"auto" -- observed as strings so neither form is lost.
int trackIdFromString(const QString &value)
{
    bool ok = false;
    const int id = value.toInt(&ok);
    return ok && id >= 0 ? id : -1;
}

const QByteArray kDefaultLogLevel = QByteArrayLiteral("warn");

QByteArray envLogLevel()
{
    return qgetenv("CUTEFIN_MPV_LOGLEVEL").trimmed();
}

// State shared between the GUI-thread MpvVideoItem and its render-thread
// renderer(s). The renderer can outlive the item (the scene graph deletes
// nodes lazily on the render thread), so every cross-thread notification goes
// through `item`, which ~MpvVideoItem nulls under `mutex` before ~QObject runs
// and removes any already-posted events.
struct RenderBridge {
    QMutex mutex;
    MpvVideoItem *item = nullptr;
    int liveRenderers = 0;
    // Coalesces render-update callbacks: one queued update() at a time.
    std::atomic_bool frameUpdatePending{false};
};

QMutex &registryMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<const MpvVideoItem *, std::shared_ptr<RenderBridge>> &registry()
{
    static QHash<const MpvVideoItem *, std::shared_ptr<RenderBridge>> map;
    return map;
}

std::shared_ptr<RenderBridge> bridgeFor(const MpvVideoItem *item)
{
    QMutexLocker locker(&registryMutex());
    return registry().value(item);
}

void *getProcAddress(void *ctx, const char *name)
{
    Q_UNUSED(ctx);
    QOpenGLContext *context = QOpenGLContext::currentContext();
    return context ? reinterpret_cast<void *>(context->getProcAddress(QByteArray(name))) : nullptr;
}

// Renders libmpv into the QQuickFramebufferObject's FBO on the scene graph
// render thread. Holds its own strong client handle on the mpv core so the
// core (which aborts if destroyed while a render context is alive) is kept
// alive until this renderer has freed its render context, regardless of
// whether the item or the renderer is destroyed first.
class MpvFboRenderer : public QQuickFramebufferObject::Renderer
{
public:
    MpvFboRenderer(mpv_handle *core, std::shared_ptr<RenderBridge> bridge)
        : m_core(core)
        , m_bridge(std::move(bridge))
    {
        QMutexLocker locker(&m_bridge->mutex);
        ++m_bridge->liveRenderers;
    }

    ~MpvFboRenderer() override
    {
        // Detach the update callback first so a late fire from an mpv
        // thread can never reach a half-destroyed renderer, then free the
        // render context (GL context is current here), then drop our core
        // reference -- if the item is already gone this is the last handle
        // and destroys the core.
        if (m_ctx) {
            mpv_render_context_set_update_callback(m_ctx, nullptr, nullptr);
            mpv_render_context_free(m_ctx);
            m_ctx = nullptr;
        }
        if (m_core) {
            mpv_destroy(m_core);
            m_core = nullptr;
        }

        QMutexLocker locker(&m_bridge->mutex);
        --m_bridge->liveRenderers;
        if (MpvVideoItem *item = m_bridge->item) {
            QMetaObject::invokeMethod(
                item, [item]() { item->notifyRenderContextDestroyed(); }, Qt::QueuedConnection);
        }
    }

    void render() override
    {
        if (!m_ctx && !m_initFailed)
            initialize();

        QOpenGLFramebufferObject *fbo = framebufferObject();
        if (!m_ctx || !fbo) {
            clearToBlack();
            QQuickOpenGLUtils::resetOpenGLState();
            return;
        }

        mpv_opengl_fbo mpvFbo;
        mpvFbo.fbo = static_cast<int>(fbo->handle());
        mpvFbo.w = fbo->width();
        mpvFbo.h = fbo->height();
        mpvFbo.internal_format = 0;

        // QQuickFramebufferObject (mirrorVertically == false) presents the
        // FBO texture with row 0 at the item's top edge, which is exactly
        // mpv's unflipped output -- the same convention mpv's own Qt Quick
        // example relies on. FLIP_Y = 1 is only for drawing straight into
        // the window's default framebuffer.
        int flipY = 0;

        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_ctx, params);

        // mpv leaves arbitrary GL state behind; Qt Quick expects it clean.
        QQuickOpenGLUtils::resetOpenGLState();
    }

private:
    static void onRenderUpdate(void *ctx)
    {
        // Fires on an mpv-internal thread. No mpv API calls allowed here;
        // just hop to the GUI thread if the item still exists.
        auto *bridge = static_cast<RenderBridge *>(ctx);
        if (bridge->frameUpdatePending.exchange(true))
            return; // an update() is already queued; it will pick this frame up
        QMutexLocker locker(&bridge->mutex);
        if (MpvVideoItem *item = bridge->item) {
            // The bridge outlives any invocation that can still run: the
            // item's registry entry holds it until ~MpvVideoItem, which also
            // discards invocations not yet delivered.
            QMetaObject::invokeMethod(
                item,
                [item, bridge]() {
                    bridge->frameUpdatePending.store(false);
                    item->requestFrameUpdate();
                },
                Qt::QueuedConnection);
        } else {
            bridge->frameUpdatePending.store(false);
        }
    }

    void initialize()
    {
        if (!m_core || !QOpenGLContext::currentContext()) {
            m_initFailed = true;
            return;
        }

        mpv_opengl_init_params glInitParams;
        glInitParams.get_proc_address = &getProcAddress;
        glInitParams.get_proc_address_ctx = nullptr;

        QList<mpv_render_param> params;
        params.append({MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)});
        params.append({MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams});

#if defined(Q_OS_LINUX)
        // Hardware decoding interop (VA-API/VDPAU) needs the native display.
#if defined(QT_FEATURE_xcb) && QT_FEATURE_xcb == 1
        if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
            if (void *display = x11->display())
                params.append({MPV_RENDER_PARAM_X11_DISPLAY, display});
        }
#endif
#if defined(QT_FEATURE_wayland) && QT_FEATURE_wayland == 1
        if (auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
            if (void *display = wayland->display())
                params.append({MPV_RENDER_PARAM_WL_DISPLAY, display});
        }
#endif
#endif

        params.append({MPV_RENDER_PARAM_INVALID, nullptr});

        const int rc = mpv_render_context_create(&m_ctx, m_core, params.data());
        if (rc < 0) {
            m_ctx = nullptr;
            m_initFailed = true;
            qCWarning(lcMpv, "mpv_render_context_create() failed: %s", mpv_error_string(rc));
            return;
        }

        mpv_render_context_set_update_callback(m_ctx, &MpvFboRenderer::onRenderUpdate, m_bridge.get());

        QMutexLocker locker(&m_bridge->mutex);
        if (MpvVideoItem *item = m_bridge->item) {
            QMetaObject::invokeMethod(
                item, [item]() { item->notifyRenderContextReady(); }, Qt::QueuedConnection);
        }
    }

    mpv_handle *m_core = nullptr;
    std::shared_ptr<RenderBridge> m_bridge;
    mpv_render_context *m_ctx = nullptr;
    bool m_initFailed = false;
};

QString nodeToString(const mpv_node &node)
{
    switch (node.format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node.u.string);
    case MPV_FORMAT_INT64:
        return QString::number(node.u.int64);
    case MPV_FORMAT_DOUBLE:
        return QString::number(node.u.double_);
    case MPV_FORMAT_FLAG:
        return node.u.flag ? QStringLiteral("yes") : QStringLiteral("no");
    default:
        return QString();
    }
}

#endif // CUTEFIN_ENABLE_MPV

} // namespace

// ---------------------------------------------------------------------------
// Construction / teardown
// ---------------------------------------------------------------------------

MpvVideoItem::MpvVideoItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
    setMirrorVertically(false); // see MpvFboRenderer::render() for the FLIP_Y pairing
    setTextureFollowsItemSize(true);

#ifdef CUTEFIN_ENABLE_MPV
    {
        auto bridge = std::make_shared<RenderBridge>();
        bridge->item = this;
        QMutexLocker locker(&registryMutex());
        registry().insert(this, bridge);
    }

    m_mpv = mpv_create();
    if (!m_mpv) {
        qCWarning(lcMpv, "mpv_create() failed");
        return;
    }

    // Options must be set before mpv_initialize().
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "force-window", "no");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    // The QML player owns input and OSD; mpv's own must not fight it.
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "input-cursor", "no");
    mpv_set_option_string(m_mpv, "cache", "yes");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "64MiB");
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "16MiB");

    {
        const QByteArray env = envLogLevel();
        const QByteArray level = env.isEmpty() ? kDefaultLogLevel : env;
        if (mpv_request_log_messages(m_mpv, level.constData()) < 0)
            mpv_request_log_messages(m_mpv, kDefaultLogLevel.constData());
        m_logLevel = QString::fromUtf8(level);
    }

    const int rc = mpv_initialize(m_mpv);
    if (rc < 0) {
        const QString error = QString::fromUtf8(mpv_error_string(rc));
        qCWarning(lcMpv, "mpv_initialize() failed: %ls", qUtf16Printable(error));
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        // Deliver after construction so QML connections exist.
        QMetaObject::invokeMethod(
            this, [this, error]() { emit mpvFatalError(QStringLiteral("mpv_initialize() failed: %1").arg(error)); },
            Qt::QueuedConnection);
        return;
    }

    observeProperties();
    mpv_set_wakeup_callback(m_mpv, &MpvVideoItem::onWakeup, this);
    // Drain anything queued before the callback was attached (initial
    // property values, early log lines).
    QMetaObject::invokeMethod(this, [this]() { processMpvEvents(); }, Qt::QueuedConnection);
#endif
}

MpvVideoItem::~MpvVideoItem()
{
#ifdef CUTEFIN_ENABLE_MPV
    std::shared_ptr<RenderBridge> bridge;
    {
        QMutexLocker locker(&registryMutex());
        bridge = registry().take(this);
    }

    int liveRenderers = 0;
    if (bridge) {
        QMutexLocker locker(&bridge->mutex);
        bridge->item = nullptr;
        liveRenderers = bridge->liveRenderers;
    }

    if (m_mpv) {
        // Detach first: a wakeup from an mpv thread must never reach a
        // half-destroyed item.
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);

        if (liveRenderers > 0) {
            // A render-thread renderer still holds a render context on this
            // core. mpv_terminate_destroy() would block until that renderer
            // frees it, which only happens on a later scene-graph sync that
            // needs this (GUI) thread -- a deadlock. Instead stop playback
            // now and drop our reference; the renderer's handle is the last
            // one and tears the core down right after freeing its context.
            const char *stopCmd[] = {"stop", nullptr};
            mpv_command(m_mpv, stopCmd);
            mpv_destroy(m_mpv);
        } else {
            mpv_terminate_destroy(m_mpv);
        }
        m_mpv = nullptr;
    }
#endif
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const
{
#ifdef CUTEFIN_ENABLE_MPV
    // Called on the render thread while the GUI thread is blocked in sync,
    // so reading m_mpv here is safe.
    if (m_mpv) {
        if (std::shared_ptr<RenderBridge> bridge = bridgeFor(this)) {
            mpv_handle *core = mpv_create_client(m_mpv, "cutefin-render");
            if (core) {
                // Nobody reads this handle's event queue; don't let it fill.
                for (int id = 1; id < 64; ++id)
                    mpv_request_event(core, static_cast<mpv_event_id>(id), 0);
                return new MpvFboRenderer(core, std::move(bridge));
            }
        }
    }
#endif
    return new StubRenderer();
}

void MpvVideoItem::notifyRenderContextReady()
{
    m_renderContextReady = true;
    if (m_pendingPlay) {
        const PendingPlay pending = *m_pendingPlay;
        m_pendingPlay.reset();
        play(pending.url, pending.startSeconds, pending.headerFields);
    }
}

void MpvVideoItem::notifyRenderContextDestroyed()
{
    m_renderContextReady = false;
}

void MpvVideoItem::requestFrameUpdate()
{
    update();
}

// ---------------------------------------------------------------------------
// Tracks
// ---------------------------------------------------------------------------

QString MpvVideoItem::redactSecrets(const QString &line)
{
    static const QRegularExpression apiKey(QStringLiteral("api_key=[^&\\s\"']+"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression token(QStringLiteral("Token=\"[^\"]+\""));
    if (!line.contains(QLatin1String("api_key"), Qt::CaseInsensitive) && !line.contains(QLatin1String("Token=")))
        return line;
    QString out = line;
    out.replace(apiKey, QStringLiteral("api_key=<redacted>"));
    out.replace(token, QStringLiteral("Token=\"<redacted>\""));
    return out;
}

void MpvVideoItem::setLogLevel(const QString &level)
{
#ifdef CUTEFIN_ENABLE_MPV
    const QByteArray env = envLogLevel();
    QByteArray wanted = env.isEmpty() ? level.trimmed().toLower().toUtf8() : env;
    if (wanted.isEmpty())
        wanted = kDefaultLogLevel;
    if (!m_mpv || QString::fromUtf8(wanted) == m_logLevel)
        return;
    if (mpv_request_log_messages(m_mpv, wanted.constData()) < 0) {
        qCWarning(lcMpv, "unknown mpv log level \"%s\"; using %s", wanted.constData(), kDefaultLogLevel.constData());
        wanted = kDefaultLogLevel;
        mpv_request_log_messages(m_mpv, wanted.constData());
    }
    m_logLevel = QString::fromUtf8(wanted);
#else
    Q_UNUSED(level);
#endif
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void MpvVideoItem::play(const QUrl &url, qint64 startSeconds, const QStringList &headerFields)
{
    if (!m_mpv) {
        qCWarning(lcMpv, "play() without a usable mpv handle; ignoring");
        emit mpvFatalError(QStringLiteral("The video player is unavailable (libmpv failed to initialize)."));
        return;
    }

#ifdef CUTEFIN_ENABLE_MPV
    if (!m_renderContextReady) {
        // vo=libmpv cannot open video output before a render context exists
        // -- loading now would play the file audio-only. Defer until the
        // renderer reports in, and nudge the scene graph to create it.
        m_pendingPlay = PendingPlay{url, startSeconds, headerFields};
        update();
        return;
    }

    const QByteArray headerValue = headerFields.join(QLatin1Char(',')).toUtf8();
    mpv_set_property_string(m_mpv, "http-header-fields", headerValue.constData());

    // Set "start" as a property instead of a loadfile per-file option: the
    // loadfile argument layout changed in mpv 0.38 (insertion index), while
    // the property works the same on every version.
    const QByteArray start = startSeconds > 0 ? QByteArray::number(startSeconds) : QByteArrayLiteral("none");
    mpv_set_property_string(m_mpv, "start", start.constData());

    m_position = static_cast<double>(qMax<qint64>(0, startSeconds));
    m_lastEmittedPosition = -1.0;
    emit positionChanged();
    m_lastEmittedPosition = m_position;

    command({QStringLiteral("loadfile"), url.toString(QUrl::FullyEncoded), QStringLiteral("replace")});
#else
    Q_UNUSED(url);
    Q_UNUSED(startSeconds);
    Q_UNUSED(headerFields);
#endif
}

void MpvVideoItem::stop()
{
    m_pendingPlay.reset();
    command({QStringLiteral("stop")});
}

void MpvVideoItem::pause()
{
    setProperty("pause", QStringLiteral("yes"));
}

void MpvVideoItem::resume()
{
    setProperty("pause", QStringLiteral("no"));
}

void MpvVideoItem::togglePause()
{
    command({QStringLiteral("cycle"), QStringLiteral("pause")});
}

void MpvVideoItem::seek(double seconds)
{
    command({QStringLiteral("seek"), QString::number(qMax(0.0, seconds), 'f', 3), QStringLiteral("absolute")});
}

void MpvVideoItem::seekRelative(double deltaSeconds)
{
    command({QStringLiteral("seek"), QString::number(deltaSeconds, 'f', 3), QStringLiteral("relative")});
}

void MpvVideoItem::setVolume(int volumePercent)
{
    const int clamped = qBound(0, volumePercent, 100);
    if (!m_mpv) {
        if (m_volume != clamped) {
            m_volume = clamped;
            emit volumeChanged();
        }
        return;
    }
    // The observed "volume" property reports back and emits volumeChanged.
    setProperty("volume", QString::number(clamped));
}

void MpvVideoItem::setMuted(bool muted)
{
    if (!m_mpv) {
        if (m_muted != muted) {
            m_muted = muted;
            emit mutedChanged();
        }
        return;
    }
    setProperty("mute", muted ? QStringLiteral("yes") : QStringLiteral("no"));
}

void MpvVideoItem::setSpeed(double speed)
{
    const double clamped = qBound(0.25, speed, 4.0);
    if (!m_mpv) {
        if (!qFuzzyCompare(m_speed, clamped)) {
            m_speed = clamped;
            emit speedChanged();
        }
        return;
    }
    setProperty("speed", QString::number(clamped, 'g', 6));
}

// The three track setters update the cached selection optimistically so a
// caller reading it back right away (auto-selection, passthrough check) sees
// the new value; the observed property event confirms or corrects it.
void MpvVideoItem::setAudioTrack(int id)
{
    const int newId = id < 0 ? -1 : id;
    if (setPropertyChecked("aid", newId < 0 ? QStringLiteral("no") : QString::number(newId))
        && newId != m_selectedAudioId) {
        m_selectedAudioId = newId;
        emit tracksChanged();
    }
}

void MpvVideoItem::setSubtitleTrack(int id)
{
    const int newId = id < 0 ? -1 : id;
    if (setPropertyChecked("sid", newId < 0 ? QStringLiteral("no") : QString::number(newId))
        && newId != m_selectedSubtitleId) {
        m_selectedSubtitleId = newId;
        emit tracksChanged();
    }
}

void MpvVideoItem::setSecondarySubtitleTrack(int id)
{
    const int newId = id < 0 ? -1 : id;
    if (setPropertyChecked("secondary-sid", newId < 0 ? QStringLiteral("no") : QString::number(newId))
        && newId != m_secondarySubId) {
        m_secondarySubId = newId;
        emit tracksChanged();
    }
}

void MpvVideoItem::addSubtitleUrl(const QUrl &url, const QString &title, const QString &language)
{
    if (!url.isValid())
        return;
    // "auto" adds the track without selecting it: the caller decides what is
    // selected (remembered per-series choice / preferred language).
    command({QStringLiteral("sub-add"), url.toString(QUrl::FullyEncoded), QStringLiteral("auto"), title, language});
}

void MpvVideoItem::frameStep()
{
    command({QStringLiteral("frame-step")});
}

void MpvVideoItem::frameBackStep()
{
    command({QStringLiteral("frame-back-step")});
}

void MpvVideoItem::cycleAspectMode()
{
    m_aspectIndex = (m_aspectIndex + 1) % 4;
    switch (m_aspectIndex) {
    case 0:
        setProperty("video-aspect-override", QStringLiteral("-1"));
        setProperty("panscan", QStringLiteral("0"));
        m_aspectMode = QStringLiteral("Fit");
        break;
    case 1:
        setProperty("video-aspect-override", QStringLiteral("-1"));
        setProperty("panscan", QStringLiteral("1"));
        m_aspectMode = QStringLiteral("Fill (Crop)");
        break;
    case 2:
        setProperty("video-aspect-override", QStringLiteral("16:9"));
        setProperty("panscan", QStringLiteral("0"));
        m_aspectMode = QStringLiteral("16:9");
        break;
    default:
        setProperty("video-aspect-override", QStringLiteral("21:9"));
        setProperty("panscan", QStringLiteral("0"));
        m_aspectMode = QStringLiteral("21:9");
        break;
    }
    emit aspectModeChanged();
}

QString MpvVideoItem::takeScreenshot()
{
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv || !m_active)
        return QString();

    QString directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (directory.isEmpty() || !QDir().mkpath(directory))
        directory = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (directory.isEmpty() || !QDir().mkpath(directory))
        directory = QDir::homePath();

    const QString fileName = QStringLiteral("cutefin-%1.png")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    const QString path = QDir(directory).filePath(fileName);

    const QByteArray pathUtf8 = QDir::toNativeSeparators(path).toUtf8();
    const char *args[] = {"screenshot-to-file", pathUtf8.constData(), "subtitles", nullptr};
    if (mpv_command(m_mpv, args) < 0)
        return QString();
    return path;
#else
    return QString();
#endif
}

void MpvVideoItem::applyPerformanceProfile(bool dualCoreMode, const QString &hwdecMode, const QString &videoProfile)
{
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv)
        return;

    setProperty("hwdec", hwdecMode.trimmed().isEmpty() ? QStringLiteral("auto-safe") : hwdecMode.trimmed());
    setProperty("demuxer-max-bytes", dualCoreMode ? QStringLiteral("48MiB") : QStringLiteral("96MiB"));

    const QString profile = videoProfile.trimmed().toLower();
    if (profile == QLatin1String("fast")) {
        command({QStringLiteral("apply-profile"), QStringLiteral("fast")});
    } else if (profile == QLatin1String("high-quality")) {
        const char *hq[] = {"apply-profile", "high-quality", nullptr};
        if (mpv_command(m_mpv, hq) < 0) {
            // mpv < 0.37 only has the older name.
            command({QStringLiteral("apply-profile"), QStringLiteral("gpu-hq")});
        }
    } else {
        // "balanced": undo whatever fast/high-quality changed by restoring
        // each touched option to mpv's own compiled-in default. Options this
        // mpv build doesn't know are skipped silently.
        static const char *const kProfileOptions[] = {
            "scale",          "dscale",           "cscale",
            "dither-depth",   "dither",           "correct-downscaling",
            "linear-downscaling", "sigmoid-upscaling", "deband",
            "hdr-compute-peak", "allow-delayed-peak-detect", "hdr-peak-percentile",
            "hdr-contrast-recovery", "scaler-lut-size",
        };
        for (const char *option : kProfileOptions) {
            const QByteArray key = QByteArrayLiteral("option-info/") + option + QByteArrayLiteral("/default-value");
            char *defaultValue = mpv_get_property_string(m_mpv, key.constData());
            if (!defaultValue)
                continue;
            mpv_set_property_string(m_mpv, option, defaultValue);
            mpv_free(defaultValue);
        }
    }
#else
    Q_UNUSED(dualCoreMode);
    Q_UNUSED(hwdecMode);
    Q_UNUSED(videoProfile);
#endif
}

// ---------------------------------------------------------------------------
// Property helpers
// ---------------------------------------------------------------------------

QString MpvVideoItem::stringProperty(const char *name) const
{
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv)
        return QString();
    char *value = mpv_get_property_string(m_mpv, name);
    if (!value)
        return QString();
    const QString result = QString::fromUtf8(value);
    mpv_free(value);
    return result;
#else
    Q_UNUSED(name);
    return QString();
#endif
}

double MpvVideoItem::doubleProperty(const char *name, double fallback) const
{
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv)
        return fallback;
    double value = 0.0;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_DOUBLE, &value) < 0)
        return fallback;
    return value;
#else
    Q_UNUSED(name);
    return fallback;
#endif
}

qint64 MpvVideoItem::intProperty(const char *name, qint64 fallback) const
{
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv)
        return fallback;
    int64_t value = 0;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_INT64, &value) < 0)
        return fallback;
    return static_cast<qint64>(value);
#else
    Q_UNUSED(name);
    return fallback;
#endif
}

bool MpvVideoItem::flagProperty(const char *name, bool fallback) const
{
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv)
        return fallback;
    int value = 0;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_FLAG, &value) < 0)
        return fallback;
    return value != 0;
#else
    Q_UNUSED(name);
    return fallback;
#endif
}

void MpvVideoItem::setProperty(const char *name, const QString &value)
{
    setPropertyChecked(name, value);
}

bool MpvVideoItem::setPropertyChecked(const char *name, const QString &value)
{
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv)
        return false;
    const QByteArray utf8 = value.toUtf8();
    const int rc = mpv_set_property_string(m_mpv, name, utf8.constData());
    if (rc < 0) {
        qCDebug(lcMpv, "set %s=%s failed: %s", name, utf8.constData(), mpv_error_string(rc));
        return false;
    }
    return true;
#else
    Q_UNUSED(name);
    Q_UNUSED(value);
    return false;
#endif
}

QList<MpvVideoItem::AudioDevice> MpvVideoItem::audioDevices() const
{
    QList<AudioDevice> devices;
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv)
        return devices;

    mpv_node node;
    if (mpv_get_property(m_mpv, "audio-device-list", MPV_FORMAT_NODE, &node) < 0)
        return devices;

    if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
        const mpv_node_list *list = node.u.list;
        for (int i = 0; i < list->num; ++i) {
            const mpv_node &entry = list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP || !entry.u.list)
                continue;
            AudioDevice device;
            const mpv_node_list *fields = entry.u.list;
            for (int j = 0; j < fields->num; ++j) {
                const char *key = fields->keys[j];
                if (qstrcmp(key, "name") == 0)
                    device.name = nodeToString(fields->values[j]);
                else if (qstrcmp(key, "description") == 0)
                    device.description = nodeToString(fields->values[j]);
            }
            if (!device.name.isEmpty())
                devices.append(device);
        }
    }
    mpv_free_node_contents(&node);
#endif
    return devices;
}

QVariantList MpvVideoItem::audioDevicesVariant() const
{
    QVariantList list;
    const QList<AudioDevice> devices = audioDevices();
    for (const AudioDevice &device : devices) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), device.name);
        map.insert(QStringLiteral("description"), device.description.isEmpty() ? device.name : device.description);
        list.append(map);
    }
    return list;
}

QStringList MpvVideoItem::applyOptionLines(const QStringList &lines)
{
    QStringList rejected;
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1String("--")))
            line = line.mid(2);

        const int separator = line.indexOf(QLatin1Char('='));
        // A bare `key` line is mpv.conf shorthand for `key=yes`.
        const QString key = separator < 0 ? line : line.left(separator).trimmed();
        QString value = separator < 0 ? QStringLiteral("yes") : line.mid(separator + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);

        if (key.isEmpty()) {
            rejected.append(rawLine.trimmed());
            continue;
        }
        const QByteArray keyUtf8 = key.toUtf8();
        if (!setPropertyChecked(keyUtf8.constData(), value))
            rejected.append(key);
    }
    return rejected;
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

QVariantMap MpvVideoItem::collectStatsSnapshot() const
{
    QVariantMap stats;

    // --- Video
    QString codec = stringProperty("video-format");
    if (codec.isEmpty())
        codec = stringProperty("video-codec");
    stats.insert(QStringLiteral("codec"), orDash(codec));

    const QString hwdec = stringProperty("hwdec-current");
    stats.insert(QStringLiteral("hwdec"),
                 hwdec.isEmpty() || hwdec == QLatin1String("no") ? QStringLiteral("software") : hwdec);

    const qint64 w = intProperty("video-params/w");
    const qint64 h = intProperty("video-params/h");
    stats.insert(QStringLiteral("resolution"), w > 0 && h > 0 ? QStringLiteral("%1×%2").arg(w).arg(h) : kDash);

    const qint64 dw = intProperty("osd-dimensions/w", intProperty("dwidth"));
    const qint64 dh = intProperty("osd-dimensions/h", intProperty("dheight"));
    stats.insert(QStringLiteral("displayRes"),
                 dw > 0 && dh > 0 ? QStringLiteral("%1×%2").arg(dw).arg(dh) : kDash);

    QString pixelFormat = stringProperty("video-params/pixelformat");
    const QString hwPixelFormat = stringProperty("video-params/hw-pixelformat");
    if (!hwPixelFormat.isEmpty())
        pixelFormat = QStringLiteral("%1 (%2)").arg(hwPixelFormat, pixelFormat);
    stats.insert(QStringLiteral("pixelFormat"), orDash(pixelFormat));

    const double containerFps = doubleProperty("container-fps");
    const double estimatedFps = doubleProperty("estimated-vf-fps");
    stats.insert(QStringLiteral("containerFps"),
                 containerFps > 0 ? QString::number(containerFps, 'f', 3) : kDash);
    stats.insert(QStringLiteral("estimatedFps"),
                 estimatedFps > 0 ? QString::number(estimatedFps, 'f', 3) : kDash);
    stats.insert(QStringLiteral("videoBitrate"), formatBitrate(doubleProperty("video-bitrate")));

    QStringList color;
    for (const char *name : {"video-params/colormatrix", "video-params/primaries", "video-params/gamma"}) {
        const QString value = stringProperty(name);
        if (!value.isEmpty())
            color << value;
    }
    const QString levels = stringProperty("video-params/colorlevels");
    if (!levels.isEmpty())
        color << levels;
    stats.insert(QStringLiteral("colorSpace"), color.isEmpty() ? kDash : color.join(QStringLiteral(" / ")));

    double peak = doubleProperty("video-params/max-luma");
    if (peak <= 0.0) {
        const double sigPeak = doubleProperty("video-params/sig-peak");
        if (sigPeak > 1.0)
            peak = sigPeak * 203.0; // sig-peak is relative to the 203 nit reference white
    }
    stats.insert(QStringLiteral("hdrPeak"), peak > 0.0 ? QStringLiteral("%1 nits").arg(peak, 0, 'f', 0) : kDash);

    // --- Audio
    QString audioCodec = stringProperty("audio-codec-name");
    if (audioCodec.isEmpty())
        audioCodec = stringProperty("audio-codec");
    stats.insert(QStringLiteral("audioCodec"), orDash(audioCodec));

    QString channels = stringProperty("audio-params/hr-channels");
    if (channels.isEmpty())
        channels = stringProperty("audio-params/channels");
    const qint64 channelCount = intProperty("audio-params/channel-count");
    if (!channels.isEmpty() && channelCount > 0)
        channels = QStringLiteral("%1 (%2 ch)").arg(channels).arg(channelCount);
    stats.insert(QStringLiteral("audioChannels"), orDash(channels));

    const qint64 sampleRate = intProperty("audio-params/samplerate");
    stats.insert(QStringLiteral("audioSampleRate"),
                 sampleRate > 0 ? QStringLiteral("%1 kHz").arg(sampleRate / 1000.0, 0, 'f', 1) : kDash);
    stats.insert(QStringLiteral("audioBitrate"), formatBitrate(doubleProperty("audio-bitrate")));

    QString audioDevice = stringProperty("audio-device");
    const QString ao = stringProperty("current-ao");
    if (!ao.isEmpty())
        audioDevice = audioDevice.isEmpty() ? ao : QStringLiteral("%1 (%2)").arg(audioDevice, ao);
    stats.insert(QStringLiteral("audioDevice"), orDash(audioDevice));

    // --- Performance
    stats.insert(QStringLiteral("droppedFrames"), intProperty("frame-drop-count"));
    stats.insert(QStringLiteral("decoderDroppedFrames"), intProperty("decoder-frame-drop-count"));
    const double avsync = doubleProperty("avsync");
    stats.insert(QStringLiteral("avSync"), QStringLiteral("%1 ms").arg(avsync * 1000.0, 0, 'f', 1));

    // --- Cache
    const double cacheDuration = doubleProperty("demuxer-cache-duration", m_cacheDuration);
    stats.insert(QStringLiteral("cacheDuration"), QStringLiteral("%1 s").arg(cacheDuration, 0, 'f', 1));
    stats.insert(QStringLiteral("cacheSpeed"), formatBytesPerSecond(doubleProperty("cache-speed")));

    // --- Logs
    stats.insert(QStringLiteral("logs"), m_recentLogs);

    return stats;
}

// ---------------------------------------------------------------------------
// Event loop
// ---------------------------------------------------------------------------

void MpvVideoItem::onWakeup(void *ctx)
{
    // Fires on an arbitrary mpv thread; hop to the GUI thread. The callback
    // is detached in the destructor before the item goes away, and Qt drops
    // already-posted invocations for a destroyed receiver.
    auto *self = static_cast<MpvVideoItem *>(ctx);
    if (self->m_wakeupPending.exchange(true))
        return; // a drain is already queued and will see the new events
    QMetaObject::invokeMethod(self, [self]() { self->processMpvEvents(); }, Qt::QueuedConnection);
}

void MpvVideoItem::processMpvEvents()
{
#ifdef CUTEFIN_ENABLE_MPV
    // Clear before draining: a wakeup that races with the loop below queues
    // a fresh drain instead of being lost.
    m_wakeupPending.store(false);
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(event);
    }
#endif
}

void MpvVideoItem::handleEvent(const mpv_event *event)
{
#ifdef CUTEFIN_ENABLE_MPV
    const auto emitPosition = [this](bool force) {
        if (force || m_lastEmittedPosition < 0.0 || qAbs(m_position - m_lastEmittedPosition) >= kPositionEmitDelta) {
            m_lastEmittedPosition = m_position;
            emit positionChanged();
        }
    };

    const auto flagValue = [](const mpv_event_property *prop, bool fallback) {
        if (prop->format == MPV_FORMAT_FLAG && prop->data)
            return *static_cast<const int *>(prop->data) != 0;
        return fallback;
    };
    const auto stringValue = [](const mpv_event_property *prop) {
        if (prop->format == MPV_FORMAT_STRING && prop->data) {
            const char *const *str = static_cast<const char *const *>(prop->data);
            return *str ? QString::fromUtf8(*str) : QString();
        }
        return QString();
    };

    switch (event->event_id) {
    case MPV_EVENT_START_FILE:
        if (!m_active) {
            m_active = true;
            emit activeChanged();
        }
        refreshBuffering();
        break;

    case MPV_EVENT_FILE_LOADED:
        if (!m_active) {
            m_active = true;
            emit activeChanged();
        }
        emitPosition(true);
        emit fileLoaded();
        break;

    case MPV_EVENT_END_FILE: {
        const auto *end = static_cast<const mpv_event_end_file *>(event->data);
        if (m_active) {
            m_active = false;
            emit activeChanged();
        }
        m_position = 0.0;
        emitPosition(true);
        resetFileState();
        if (end) {
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                emit mpvFatalError(
                    QStringLiteral("Playback failed: %1").arg(QString::fromUtf8(mpv_error_string(end->error))));
            } else if (end->reason == MPV_END_FILE_REASON_EOF) {
                emit endOfFile();
            }
        }
        break;
    }

    case MPV_EVENT_PLAYBACK_RESTART:
        // Fired once a seek (or the initial load) has completed and the
        // position is accurate again.
        emitPosition(true);
        refreshBuffering();
        emit seeked();
        break;

    case MPV_EVENT_LOG_MESSAGE: {
        const auto *msg = static_cast<const mpv_event_log_message *>(event->data);
        if (!msg)
            break;
        // mpv echoes stream URLs (api_key=...) and request headers
        // (Authorization: MediaBrowser ... Token="...") at info/v levels;
        // nothing downstream -- debug output, the stats overlay, copied
        // diagnostics -- may ever see the credential.
        const QString line = redactSecrets(QStringLiteral("[%1] %2: %3")
                                               .arg(QString::fromUtf8(msg->level), QString::fromUtf8(msg->prefix),
                                                    QString::fromUtf8(msg->text).trimmed()));
        m_recentLogs.append(line);
        while (m_recentLogs.size() > kMaxRecentLogs)
            m_recentLogs.removeFirst();
        qCDebug(lcMpv, "%ls", qUtf16Printable(line));
        emit mpvLogMessage(line);
        break;
    }

    case MPV_EVENT_PROPERTY_CHANGE: {
        const auto *prop = static_cast<const mpv_event_property *>(event->data);
        if (!prop || !prop->name)
            break;
        const char *name = prop->name;

        if (qstrcmp(name, "time-pos") == 0) {
            if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                m_position = *static_cast<const double *>(prop->data);
                emitPosition(false);
            }
        } else if (qstrcmp(name, "duration") == 0) {
            const double value =
                prop->format == MPV_FORMAT_DOUBLE && prop->data ? *static_cast<const double *>(prop->data) : 0.0;
            if (!qFuzzyCompare(value + 1.0, m_duration + 1.0)) {
                m_duration = value;
                emit durationChanged();
            }
        } else if (qstrcmp(name, "pause") == 0) {
            if (prop->format == MPV_FORMAT_FLAG && prop->data) {
                const bool paused = *static_cast<const int *>(prop->data) != 0;
                if (paused != m_paused) {
                    m_paused = paused;
                    emit pausedChanged();
                    emitPosition(true);
                }
                refreshBuffering();
            }
        } else if (qstrcmp(name, "paused-for-cache") == 0) {
            m_pausedForCache = flagValue(prop, false);
            refreshBuffering();
        } else if (qstrcmp(name, "core-idle") == 0) {
            m_coreIdle = flagValue(prop, true);
            refreshBuffering();
        } else if (qstrcmp(name, "eof-reached") == 0) {
            const bool eof = flagValue(prop, false);
            const bool reached = eof && !m_eofReached;
            m_eofReached = eof;
            refreshBuffering();
            if (reached) {
                emitPosition(true);
                emit endOfFile();
            }
        } else if (qstrcmp(name, "aid") == 0 || qstrcmp(name, "sid") == 0 || qstrcmp(name, "secondary-sid") == 0) {
            const int id = trackIdFromString(stringValue(prop));
            int &slot = qstrcmp(name, "aid") == 0   ? m_selectedAudioId
                        : qstrcmp(name, "sid") == 0 ? m_selectedSubtitleId
                                                    : m_secondarySubId;
            if (slot != id) {
                slot = id;
                emit tracksChanged();
            }
        } else if (qstrcmp(name, "demuxer-cache-duration") == 0) {
            const double value =
                prop->format == MPV_FORMAT_DOUBLE && prop->data ? *static_cast<const double *>(prop->data) : 0.0;
            if (qAbs(value - m_cacheDuration) >= kCacheEmitDelta || (value == 0.0 && m_cacheDuration != 0.0)) {
                m_cacheDuration = value;
                emit cacheDurationChanged();
            }
        } else if (qstrcmp(name, "track-list") == 0) {
            if (prop->format == MPV_FORMAT_NODE && prop->data)
                updateTracksFromNode(prop->data);
        } else if (qstrcmp(name, "volume") == 0) {
            if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                const int volume = qRound(*static_cast<const double *>(prop->data));
                if (volume != m_volume) {
                    m_volume = volume;
                    emit volumeChanged();
                }
            }
        } else if (qstrcmp(name, "mute") == 0) {
            if (prop->format == MPV_FORMAT_FLAG && prop->data) {
                const bool muted = *static_cast<const int *>(prop->data) != 0;
                if (muted != m_muted) {
                    m_muted = muted;
                    emit mutedChanged();
                }
            }
        } else if (qstrcmp(name, "speed") == 0) {
            if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                const double speed = *static_cast<const double *>(prop->data);
                if (!qFuzzyCompare(speed, m_speed)) {
                    m_speed = speed;
                    emit speedChanged();
                }
            }
        }
        break;
    }

    default:
        break;
    }
#else
    Q_UNUSED(event);
#endif
}

void MpvVideoItem::observeProperties()
{
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv)
        return;
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "demuxer-cache-duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "aid", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "sid", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "secondary-sid", MPV_FORMAT_STRING);
#endif
}

void MpvVideoItem::refreshBuffering()
{
    // Purely from cached, observed state -- no synchronous property reads.
    const bool buffering = m_active && (m_pausedForCache || (m_coreIdle && !m_paused && !m_eofReached));
    if (buffering != m_buffering) {
        m_buffering = buffering;
        emit bufferingChanged();
    }
}

void MpvVideoItem::resetFileState()
{
    if (m_duration != 0.0) {
        m_duration = 0.0;
        emit durationChanged();
    }
    if (m_cacheDuration != 0.0) {
        m_cacheDuration = 0.0;
        emit cacheDurationChanged();
    }
    // Observed values (aid/sid/secondary-sid, eof-reached, paused-for-cache,
    // core-idle) are deliberately left alone: mpv only reports changes
    // relative to what it last sent, so resetting them here could leave the
    // cache permanently out of sync. mpv updates them itself as the next
    // file starts.
    if (!m_tracks.isEmpty()) {
        m_tracks.clear();
        m_audioTracksVariant.clear();
        m_subtitleTracksVariant.clear();
        emit tracksChanged();
    }
    if (m_buffering) {
        m_buffering = false;
        emit bufferingChanged();
    }
}

void MpvVideoItem::updateTracksFromNode(void *nodePtr)
{
#ifdef CUTEFIN_ENABLE_MPV
    const auto *node = static_cast<const mpv_node *>(nodePtr);
    QList<Track> tracks;

    if (node && node->format == MPV_FORMAT_NODE_ARRAY && node->u.list) {
        const mpv_node_list *list = node->u.list;
        for (int i = 0; i < list->num; ++i) {
            const mpv_node &entry = list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP || !entry.u.list)
                continue;

            const mpv_node_list *fields = entry.u.list;
            Track track;
            for (int j = 0; j < fields->num; ++j) {
                const char *key = fields->keys[j];
                const mpv_node &value = fields->values[j];
                const bool flag = value.format == MPV_FORMAT_FLAG && value.u.flag != 0;
                if (qstrcmp(key, "id") == 0 && value.format == MPV_FORMAT_INT64)
                    track.id = static_cast<int>(value.u.int64);
                else if (qstrcmp(key, "type") == 0)
                    track.type = nodeToString(value);
                else if (qstrcmp(key, "lang") == 0)
                    track.lang = nodeToString(value);
                else if (qstrcmp(key, "title") == 0)
                    track.title = nodeToString(value);
                else if (qstrcmp(key, "codec") == 0)
                    track.codec = nodeToString(value);
                else if (qstrcmp(key, "selected") == 0)
                    track.selected = flag;
                else if (qstrcmp(key, "forced") == 0)
                    track.forced = flag;
                else if (qstrcmp(key, "hearing-impaired") == 0)
                    track.hearingImpaired = flag;
                else if (qstrcmp(key, "external") == 0)
                    track.external = flag;
            }
            if (track.id >= 0)
                tracks.append(track);
        }
    }

    QVariantList audio;
    QVariantList subs;
    for (const Track &track : std::as_const(tracks)) {
        if (track.type == QLatin1String("audio"))
            audio.append(trackToVariant(track));
        else if (track.type == QLatin1String("sub"))
            subs.append(trackToVariant(track));
    }

    m_tracks = tracks;
    m_audioTracksVariant = audio;
    m_subtitleTracksVariant = subs;
    emit tracksChanged();
#else
    Q_UNUSED(nodePtr);
#endif
}

void MpvVideoItem::command(const QStringList &args)
{
#ifdef CUTEFIN_ENABLE_MPV
    if (!m_mpv || args.isEmpty())
        return;

    QList<QByteArray> storage;
    storage.reserve(args.size());
    for (const QString &arg : args)
        storage.append(arg.toUtf8());

    QList<const char *> cArgs;
    cArgs.reserve(storage.size() + 1);
    for (const QByteArray &bytes : storage)
        cArgs.append(bytes.constData());
    cArgs.append(nullptr);

    const int rc = mpv_command(m_mpv, cArgs.data());
    if (rc < 0)
        qCDebug(lcMpv, "command %s failed: %s", storage.first().constData(), mpv_error_string(rc));
#else
    Q_UNUSED(args);
#endif
}
