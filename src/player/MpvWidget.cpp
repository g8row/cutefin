#include "player/MpvWidget.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace {

Q_LOGGING_CATEGORY(lcMpv, "cutefin.mpv")

QByteArray mpvLogLevel()
{
    const QByteArray env = qgetenv("CUTEFIN_MPV_LOGLEVEL");
    return env.isEmpty() ? QByteArrayLiteral("warn") : env;
}

} // namespace

MpvWidget::MpvWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);

    m_mpv = mpv_create();
    if (!m_mpv) {
        qCWarning(lcMpv, "mpv_create() failed");
        emit mpvFatalError(QStringLiteral("mpv_create() failed"));
        return;
    }

    // Options must be set before mpv_initialize() (DESIGN.md §7).
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "force-window", "no");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "ytdl", "no");
    // We own input/OSD via our own controls overlay; don't let mpv's
    // defaults fight with them.
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "input-cursor", "no");

    mpv_request_log_messages(m_mpv, mpvLogLevel().constData());

    const int initRc = mpv_initialize(m_mpv);
    if (initRc < 0) {
        const QString error = QString::fromUtf8(mpv_error_string(initRc));
        qCWarning(lcMpv, "mpv_initialize() failed: %ls", qUtf16Printable(error));
        emit mpvFatalError(QStringLiteral("mpv_initialize() failed: %1").arg(error));
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_set_wakeup_callback(m_mpv, &MpvWidget::onWakeup, this);
    observeProperties();
}

MpvWidget::~MpvWidget()
{
    // Destruction order is the classic crash source here: detach the
    // callbacks first (a late fire from an mpv-internal thread must not
    // touch a half-destroyed widget), then makeCurrent -> free the render
    // context -> doneCurrent -> terminate_destroy the core (DESIGN.md §7).
    if (m_mpvGl)
        mpv_render_context_set_update_callback(m_mpvGl, nullptr, nullptr);
    if (m_mpv)
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);

    if (m_mpvGl) {
        makeCurrent();
        mpv_render_context_free(m_mpvGl);
        m_mpvGl = nullptr;
        doneCurrent();
    }

    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

void MpvWidget::initializeGL()
{
    initializeOpenGLFunctions();

    if (!m_mpv || m_mpvGl)
        return;

    mpv_opengl_init_params glInitParams;
    glInitParams.get_proc_address = [](void *ctx, const char *name) -> void * {
        Q_UNUSED(ctx);
        QOpenGLContext *glContext = QOpenGLContext::currentContext();
        return glContext ? reinterpret_cast<void *>(glContext->getProcAddress(name)) : nullptr;
    };
    glInitParams.get_proc_address_ctx = nullptr;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    const int renderRc = mpv_render_context_create(&m_mpvGl, m_mpv, params);
    if (renderRc < 0) {
        m_mpvGl = nullptr;
        const QString error = QString::fromUtf8(mpv_error_string(renderRc));
        qCWarning(lcMpv, "mpv_render_context_create() failed: %ls", qUtf16Printable(error));
        emit mpvFatalError(QStringLiteral("mpv_render_context_create() failed: %1").arg(error));
        return;
    }

    mpv_render_context_set_update_callback(m_mpvGl, &MpvWidget::onRenderUpdate, this);
}

void MpvWidget::paintGL()
{
    if (!m_mpvGl)
        return;

    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo;
    fbo.fbo = static_cast<int>(defaultFramebufferObject());
    fbo.w = static_cast<int>(width() * dpr);
    fbo.h = static_cast<int>(height() * dpr);
    fbo.internal_format = 0;

    int flipY = 1;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    mpv_render_context_render(m_mpvGl, params);
}

void MpvWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
    // Nothing extra needed: paintGL() reads width()/height() fresh every
    // frame, so a resize just changes what the next paintGL() reports.
}

void MpvWidget::onRenderUpdate(void *ctx)
{
    // Fires on an mpv-internal render thread. Never touch Qt here directly;
    // hop to the GUI thread via a queued, context-guarded invocation (Qt
    // cancels this automatically if the widget is destroyed in the
    // meantime).
    auto *self = static_cast<MpvWidget *>(ctx);
    QMetaObject::invokeMethod(
        self, [self]() { self->update(); }, Qt::QueuedConnection);
}

void MpvWidget::onWakeup(void *ctx)
{
    // Fires on an arbitrary mpv-internal thread.
    auto *self = static_cast<MpvWidget *>(ctx);
    QMetaObject::invokeMethod(
        self, [self]() { self->processMpvEvents(); }, Qt::QueuedConnection);
}

void MpvWidget::processMpvEvents()
{
    if (!m_mpv)
        return;

    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(event);
    }
}

void MpvWidget::handleEvent(const mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_FILE_LOADED:
        emit fileLoaded();
        break;
    case MPV_EVENT_END_FILE:
        emit endOfFile();
        break;
    case MPV_EVENT_SEEK:
        emit seeked();
        break;
    case MPV_EVENT_LOG_MESSAGE: {
        const auto *msg = static_cast<const mpv_event_log_message *>(event->data);
        if (msg) {
            emit mpvLogMessage(QStringLiteral("[%1] %2: %3")
                                    .arg(QString::fromUtf8(msg->level), QString::fromUtf8(msg->prefix),
                                         QString::fromUtf8(msg->text).trimmed()));
        }
        break;
    }
    case MPV_EVENT_PROPERTY_CHANGE: {
        const auto *prop = static_cast<const mpv_event_property *>(event->data);
        if (!prop || !prop->name)
            break;
        const QString name = QString::fromUtf8(prop->name);
        if (name == QLatin1String("time-pos")) {
            if (prop->format == MPV_FORMAT_DOUBLE) {
                m_position = static_cast<qint64>(*static_cast<double *>(prop->data));
                emit positionChanged(m_position);
            }
        } else if (name == QLatin1String("duration")) {
            if (prop->format == MPV_FORMAT_DOUBLE) {
                m_duration = static_cast<qint64>(*static_cast<double *>(prop->data));
                emit durationChanged(m_duration);
            }
        } else if (name == QLatin1String("pause")) {
            if (prop->format == MPV_FORMAT_FLAG) {
                m_paused = *static_cast<int *>(prop->data) != 0;
                emit pausedChanged(m_paused);
            }
        } else if (name == QLatin1String("core-idle")) {
            if (prop->format == MPV_FORMAT_FLAG)
                emit coreIdleChanged(*static_cast<int *>(prop->data) != 0);
        } else if (name == QLatin1String("eof-reached")) {
            if (prop->format == MPV_FORMAT_FLAG && *static_cast<int *>(prop->data) != 0)
                emit endOfFile();
        } else if (name == QLatin1String("track-list")) {
            if (prop->format == MPV_FORMAT_NODE)
                updateTracksFromNode(prop->data);
        }
        break;
    }
    default:
        break;
    }
}

void MpvWidget::observeProperties()
{
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NODE);
}

void MpvWidget::updateTracksFromNode(void *nodePtr)
{
    auto *node = static_cast<mpv_node *>(nodePtr);
    QList<Track> tracks;

    if (node && node->format == MPV_FORMAT_NODE_ARRAY && node->u.list) {
        mpv_node_list *list = node->u.list;
        for (int i = 0; i < list->num; ++i) {
            const mpv_node &entry = list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP || !entry.u.list)
                continue;

            mpv_node_list *fields = entry.u.list;
            Track track;
            for (int j = 0; j < fields->num; ++j) {
                const QString key = QString::fromUtf8(fields->keys[j]);
                const mpv_node &value = fields->values[j];
                if (key == QLatin1String("id") && value.format == MPV_FORMAT_INT64) {
                    track.id = static_cast<int>(value.u.int64);
                } else if (key == QLatin1String("type") && value.format == MPV_FORMAT_STRING) {
                    track.type = QString::fromUtf8(value.u.string);
                } else if (key == QLatin1String("lang") && value.format == MPV_FORMAT_STRING) {
                    track.lang = QString::fromUtf8(value.u.string);
                } else if (key == QLatin1String("title") && value.format == MPV_FORMAT_STRING) {
                    track.title = QString::fromUtf8(value.u.string);
                } else if (key == QLatin1String("selected") && value.format == MPV_FORMAT_FLAG) {
                    track.selected = value.u.flag != 0;
                }
            }
            tracks.append(track);
        }
    }

    m_tracks = tracks;
    emit tracksChanged(m_tracks);
}

void MpvWidget::command(const QStringList &args)
{
    if (!m_mpv)
        return;

    QList<QByteArray> storage;
    storage.reserve(args.size());
    for (const QString &arg : args)
        storage.append(arg.toUtf8());

    QList<const char *> cArgs;
    cArgs.reserve(storage.size() + 1);
    for (const QByteArray &b : storage)
        cArgs.append(b.constData());
    cArgs.append(nullptr);

    mpv_command(m_mpv, cArgs.data());
}

void MpvWidget::play(const QUrl &url, qint64 startSeconds, const QStringList &headerFields)
{
    if (!m_mpv) {
        qCWarning(lcMpv, "play() called with no usable mpv handle (initialization failed earlier); ignoring");
        return;
    }

    const QByteArray headerValue = headerFields.join(QStringLiteral(",")).toUtf8();
    mpv_set_option_string(m_mpv, "http-header-fields", headerValue.constData());

    QStringList args{QStringLiteral("loadfile"), url.toString(QUrl::FullyEncoded), QStringLiteral("replace")};
    if (startSeconds > 0) {
        // loadfile's signature is "loadfile <url> [<flags> [<index> [<options>]]]";
        // since mpv 0.38 the insertion-index argument must be passed (as "-1",
        // meaning "not applicable") whenever the trailing per-file options
        // argument is used, or mpv tries to parse the options string as that
        // index and rejects it.
        args << QStringLiteral("-1") << QStringLiteral("start=%1").arg(startSeconds);
    }

    command(args);
}

void MpvWidget::stop()
{
    command({QStringLiteral("stop")});
}

void MpvWidget::pause()
{
    if (!m_mpv)
        return;
    int flag = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvWidget::resume()
{
    if (!m_mpv)
        return;
    int flag = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvWidget::togglePause()
{
    command({QStringLiteral("cycle"), QStringLiteral("pause")});
}

void MpvWidget::seek(qint64 seconds)
{
    command({QStringLiteral("seek"), QString::number(seconds), QStringLiteral("absolute")});
}

void MpvWidget::seekRelative(qint64 deltaSeconds)
{
    command({QStringLiteral("seek"), QString::number(deltaSeconds), QStringLiteral("relative")});
}

void MpvWidget::setVolume(int volumePercent)
{
    m_volume = qBound(0, volumePercent, 100);
    if (!m_mpv)
        return;
    double v = m_volume;
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &v);
}

void MpvWidget::setMuted(bool muted)
{
    m_muted = muted;
    if (!m_mpv)
        return;
    int flag = muted ? 1 : 0;
    mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag);
}

void MpvWidget::setAudioTrack(int id)
{
    if (!m_mpv)
        return;
    const QByteArray value = id < 0 ? QByteArrayLiteral("no") : QByteArray::number(id);
    mpv_set_property_string(m_mpv, "aid", value.constData());
}

void MpvWidget::setSubtitleTrack(int id)
{
    if (!m_mpv)
        return;
    const QByteArray value = id < 0 ? QByteArrayLiteral("no") : QByteArray::number(id);
    mpv_set_property_string(m_mpv, "sid", value.constData());
}
