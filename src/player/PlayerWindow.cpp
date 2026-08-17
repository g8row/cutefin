#include "player/PlayerWindow.h"

#include "api/JellyfinClient.h"
#include "core/Formatting.h"
#include "player/PlaybackReporter.h"
#include "player/SeekBar.h"
#include "player/StatsOverlay.h"
#include "player/TrickplayCache.h"

#include <QApplication>
#include <QClipboard>

#include <QAction>
#include <QCloseEvent>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int kHideDelayMs = 2500;
constexpr int kFadeDurationMs = 180;
constexpr int kOverlayHeight = 84;
constexpr int kOverlayMargin = 16;

// Small hairline bar behind the controls. Painted from palette().shadow()
// with alpha per DESIGN.md §6 -- no hardcoded colors, no stylesheet colors.
class OverlayBar : public QWidget
{
public:
    explicit OverlayBar(QWidget *parent)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor scrim = palette().color(QPalette::Shadow);
        scrim.setAlpha(180);
        painter.setPen(Qt::NoPen);
        painter.setBrush(scrim);
        painter.drawRoundedRect(rect(), 10, 10);
    }
};

QString trackLabel(const MpvWidget::Track &track)
{
    if (!track.title.isEmpty())
        return track.title;
    if (!track.lang.isEmpty())
        return track.lang;
    return QStringLiteral("Track %1").arg(track.id);
}

} // namespace

PlayerWindow::PlayerWindow(JellyfinClient *client, QWidget *parent)
    : QWidget(parent)
    , m_mpv(new MpvWidget(this))
    , m_client(client)
{
    setWindowTitle(QStringLiteral("cutefin"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(480, 270);

    if (m_client) {
        m_reporter = new PlaybackReporter(m_client, m_mpv, this);
        m_trickplay = new TrickplayCache(m_client, this);
    }

    buildUi();

    m_stats = new StatsOverlay(m_mpv, this);
    m_stats->hide();
    if (m_trickplay)
        m_seekSlider->setTrickplayCache(m_trickplay);

    connect(m_mpv, &MpvWidget::positionChanged, this, &PlayerWindow::onPositionChanged);
    connect(m_mpv, &MpvWidget::durationChanged, this, &PlayerWindow::onDurationChanged);
    connect(m_mpv, &MpvWidget::pausedChanged, this, &PlayerWindow::onPausedChanged);
    connect(m_mpv, &MpvWidget::tracksChanged, this, &PlayerWindow::onTracksChanged);
    connect(m_mpv, &MpvWidget::mpvFatalError, this,
            [](const QString &message) { qWarning("MpvWidget: %ls", qUtf16Printable(message)); });
    connect(m_mpv, &MpvWidget::mpvLogMessage, this, [this](const QString &message) {
        qDebug("mpv: %ls", qUtf16Printable(message));
        if (m_stats)
            m_stats->appendLogLine(message);
    });

    m_mpv->setVolume(100);

    revealControls();
}

PlayerWindow::~PlayerWindow()
{
    if (m_reporter)
        m_reporter->stop();
}

void PlayerWindow::buildUi()
{
    m_mpv->setMouseTracking(true);
    m_mpv->installEventFilter(this);

    m_overlay = new OverlayBar(this);
    m_overlay->setMouseTracking(true);
    m_overlay->installEventFilter(this);

    auto *overlayLayout = new QVBoxLayout(m_overlay);
    overlayLayout->setContentsMargins(14, 8, 14, 8);
    overlayLayout->setSpacing(4);

    auto *seekRow = new QHBoxLayout();
    m_positionLabel = new QLabel(QStringLiteral("0:00"), m_overlay);
    m_durationLabel = new QLabel(QStringLiteral("0:00"), m_overlay);
    m_seekSlider = new SeekBar(m_overlay);
    m_seekSlider->setRange(0, 0);
    m_seekSlider->setMouseTracking(true);
    seekRow->addWidget(m_positionLabel);
    seekRow->addWidget(m_seekSlider, 1);
    seekRow->addWidget(m_durationLabel);

    auto *buttonRow = new QHBoxLayout();
    m_playPauseButton = new QToolButton(m_overlay);
    m_playPauseButton->setAutoRaise(true);
    m_muteButton = new QToolButton(m_overlay);
    m_muteButton->setAutoRaise(true);
    m_volumeSlider = new QSlider(Qt::Horizontal, m_overlay);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(90);
    m_chapterButton = new QToolButton(m_overlay);
    m_chapterButton->setAutoRaise(true);
    m_chapterButton->setText(QStringLiteral("Chapters"));
    m_chapterButton->hide(); // shown only once we know the item has chapters
    m_audioTrackButton = new QToolButton(m_overlay);
    m_audioTrackButton->setAutoRaise(true);
    m_audioTrackButton->setText(QStringLiteral("Audio"));
    m_audioTrackButton->setEnabled(false);
    m_subtitleTrackButton = new QToolButton(m_overlay);
    m_subtitleTrackButton->setAutoRaise(true);
    m_subtitleTrackButton->setText(QStringLiteral("Subtitles"));
    m_subtitleTrackButton->setEnabled(false);
    m_fullscreenButton = new QToolButton(m_overlay);
    m_fullscreenButton->setAutoRaise(true);
    m_fullscreenButton->setText(QStringLiteral("⛶"));
    m_fullscreenButton->setToolTip(QStringLiteral("Toggle fullscreen (F)"));

    buttonRow->addWidget(m_playPauseButton);
    buttonRow->addWidget(m_muteButton);
    buttonRow->addWidget(m_volumeSlider);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_chapterButton);
    buttonRow->addWidget(m_audioTrackButton);
    buttonRow->addWidget(m_subtitleTrackButton);
    buttonRow->addWidget(m_fullscreenButton);

    overlayLayout->addLayout(seekRow);
    overlayLayout->addLayout(buttonRow);

    updatePlayPauseIcon();
    updateMuteIcon();

    connect(m_playPauseButton, &QToolButton::clicked, this, &PlayerWindow::onPlayPauseClicked);
    connect(m_muteButton, &QToolButton::clicked, this, &PlayerWindow::onMuteClicked);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &PlayerWindow::onVolumeSliderChanged);
    connect(m_chapterButton, &QToolButton::clicked, this, &PlayerWindow::showChapterMenu);
    connect(m_audioTrackButton, &QToolButton::clicked, this, &PlayerWindow::showAudioTrackMenu);
    connect(m_subtitleTrackButton, &QToolButton::clicked, this, &PlayerWindow::showSubtitleTrackMenu);
    connect(m_fullscreenButton, &QToolButton::clicked, this, &PlayerWindow::toggleFullscreen);
    connect(m_seekSlider, &QSlider::sliderPressed, this, &PlayerWindow::onSeekSliderPressed);
    connect(m_seekSlider, &QSlider::sliderReleased, this, &PlayerWindow::onSeekSliderReleased);
    connect(m_seekSlider, &QSlider::sliderMoved, this, &PlayerWindow::onSeekSliderMoved);
    connect(m_seekSlider, &SeekBar::seekRequested, this, [this](qint64 seconds) { m_mpv->seek(seconds); });

    m_overlayOpacity = new QGraphicsOpacityEffect(m_overlay);
    m_overlayOpacity->setOpacity(1.0);
    m_overlay->setGraphicsEffect(m_overlayOpacity);

    m_overlayAnimation = new QPropertyAnimation(m_overlayOpacity, "opacity", this);
    m_overlayAnimation->setDuration(kFadeDurationMs);
    m_overlayAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(kHideDelayMs);
    connect(m_hideTimer, &QTimer::timeout, this, &PlayerWindow::hideControls);

    layoutOverlay();
}

void PlayerWindow::layoutOverlay()
{
    m_mpv->setGeometry(rect());
    if (m_stats) {
        m_stats->setGeometry(kOverlayMargin, kOverlayMargin, qMin(560, width() - 2 * kOverlayMargin),
                              qMax(0, height() - 2 * kOverlayMargin - kOverlayHeight - kOverlayMargin));
    }
    m_overlay->setGeometry(kOverlayMargin, height() - kOverlayMargin - kOverlayHeight, width() - 2 * kOverlayMargin,
                            kOverlayHeight);
}

void PlayerWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutOverlay();
}

void PlayerWindow::playItem(const QString &itemId, const QString &playSessionId, const QString &mediaSourceId,
                             const QUrl &url, qint64 startSeconds, const QStringList &httpHeaderFields)
{
    m_itemId = itemId;
    m_playSessionId = playSessionId;
    m_mediaSourceId = mediaSourceId;

    m_mpv->play(url, startSeconds, httpHeaderFields);
    if (m_reporter)
        m_reporter->start(itemId, playSessionId, mediaSourceId, Formatting::secondsToTicks(startSeconds));
}

void PlayerWindow::setPlaybackDetails(const MediaItem &item, bool transcoding, const QStringList &transcodeReasons)
{
    m_seekSlider->setChapters(item.chapters, m_durationSeconds);
    m_chapterButton->setVisible(!item.chapters.isEmpty());

    // Trickplay is published per media source and per tile width. Prefer the
    // source actually being played; fall back to whatever single source the
    // item has, since /PlaybackInfo can hand back an id that is not a key
    // here for remuxed sources.
    if (m_trickplay) {
        QHash<int, TrickplayInfo> byWidth = item.trickplay.value(m_mediaSourceId);
        if (byWidth.isEmpty() && item.trickplay.size() == 1)
            byWidth = item.trickplay.constBegin().value();

        TrickplayInfo chosen;
        int chosenWidth = 0;
        for (auto it = byWidth.constBegin(); it != byWidth.constEnd(); ++it) {
            // Widest available: these are already small (320px on the test
            // server) and the preview popup scales down far better than up.
            if (it.key() > chosenWidth) {
                chosenWidth = it.key();
                chosen = it.value();
            }
        }
        m_trickplay->setSource(item.id, chosen);
    }

    if (m_stats) {
        m_stats->setPlaybackContext(m_playSessionId, m_mediaSourceId, transcoding, transcodeReasons,
                                     m_client ? QUrl(m_client->serverUrl()).host() : QString());
    }

    loadExternalSubtitles(item);
}

void PlayerWindow::loadExternalSubtitles(const MediaItem &item)
{
    if (!m_client || m_itemId.isEmpty())
        return;

    // mpv only sees tracks inside the container. Jellyfin's IsExternal
    // subtitles live beside the file, so without this they simply do not
    // appear in the subtitle menu at all.
    m_client->fetchMediaSources(m_itemId, this,
                                 [this](bool ok, const QList<MediaSource> &sources, const QString &error) {
                                     if (!ok) {
                                         qWarning() << "PlayerWindow: media sources failed:" << error;
                                         return;
                                     }
                                     for (const MediaSource &source : sources) {
                                         if (!m_mediaSourceId.isEmpty() && source.id != m_mediaSourceId)
                                             continue;
                                         for (const MediaStream &stream : source.streams) {
                                             if (stream.type != QStringLiteral("Subtitle") || !stream.isExternal)
                                                 continue;
                                             m_mpv->addSubtitleUrl(
                                                 m_client->subtitleStreamUrl(m_itemId, source.id, stream.index),
                                                 stream.displayTitle, stream.language);
                                         }
                                     }
                                 });
    Q_UNUSED(item);
}

void PlayerWindow::jumpChapter(int direction)
{
    const QList<Chapter> &chapters = m_seekSlider->chapters();
    if (chapters.isEmpty()) {
        seekRelativeSeconds(direction * 60);
        return;
    }

    const qint64 position = m_mpv->position();
    if (direction < 0) {
        // Match every other player: "previous" within the first few seconds
        // of a chapter means the one before it, otherwise restart this one.
        for (int i = chapters.size() - 1; i >= 0; --i) {
            const qint64 start = Formatting::ticksToSeconds(chapters.at(i).startTicks);
            if (start < position - 3) {
                m_mpv->seek(start);
                return;
            }
        }
        m_mpv->seek(0);
        return;
    }

    for (const Chapter &chapter : chapters) {
        const qint64 start = Formatting::ticksToSeconds(chapter.startTicks);
        if (start > position) {
            m_mpv->seek(start);
            return;
        }
    }
}

void PlayerWindow::showChapterMenu()
{
    const QList<Chapter> &chapters = m_seekSlider->chapters();
    if (chapters.isEmpty())
        return;

    QMenu menu(this);
    const int current = m_seekSlider->chapterIndexAt(m_mpv->position());
    for (int i = 0; i < chapters.size(); ++i) {
        const Chapter &chapter = chapters.at(i);
        const qint64 seconds = Formatting::ticksToSeconds(chapter.startTicks);
        const QString label = chapter.name.isEmpty() ? tr("Chapter %1").arg(i + 1) : chapter.name;
        QAction *action = menu.addAction(QStringLiteral("%1  —  %2").arg(Formatting::formatPosition(seconds), label));
        action->setCheckable(true);
        action->setChecked(i == current);
        connect(action, &QAction::triggered, this, [this, seconds] { m_mpv->seek(seconds); });
    }
    menu.exec(m_chapterButton->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void PlayerWindow::toggleStatsOverlay()
{
    if (!m_stats)
        return;
    m_stats->setVisible(!m_stats->isVisible());
    if (m_stats->isVisible()) {
        m_stats->raise();
        layoutOverlay();
    }
}

void PlayerWindow::copyDiagnostics()
{
    if (!m_stats)
        return;
    QApplication::clipboard()->setText(m_stats->diagnosticsText());
}

void PlayerWindow::adjustSpeed(double factor)
{
    // mpv accepts far wider, but past these bounds audio is unusable and it
    // stops being a feature and starts being a way to get stuck.
    const double next = qBound(0.25, m_mpv->speed() * factor, 4.0);
    m_mpv->setSpeed(next);
}

void PlayerWindow::playUrl(const QUrl &url, qint64 startSeconds, const QString &title)
{
    if (!title.isEmpty())
        setWindowTitle(title);
    m_mpv->play(url, startSeconds);
}

void PlayerWindow::onPlayPauseClicked()
{
    m_mpv->togglePause();
}

void PlayerWindow::onSeekSliderPressed()
{
    m_seekSliderDragging = true;
}

void PlayerWindow::onSeekSliderReleased()
{
    m_seekSliderDragging = false;
    m_mpv->seek(m_seekSlider->value());
}

void PlayerWindow::onSeekSliderMoved(int value)
{
    m_positionLabel->setText(Formatting::formatPosition(value));
}

void PlayerWindow::onPositionChanged(qint64 seconds)
{
    if (!m_seekSliderDragging) {
        QSignalBlocker blocker(m_seekSlider);
        m_seekSlider->setValue(static_cast<int>(seconds));
        m_positionLabel->setText(Formatting::formatPosition(seconds));
    }
}

void PlayerWindow::onDurationChanged(qint64 seconds)
{
    m_durationSeconds = seconds;
    m_seekSlider->setRange(0, static_cast<int>(seconds));
    m_durationLabel->setText(Formatting::formatPosition(seconds));
}

void PlayerWindow::onPausedChanged(bool paused)
{
    Q_UNUSED(paused);
    updatePlayPauseIcon();
}

void PlayerWindow::onTracksChanged(const QList<MpvWidget::Track> &tracks)
{
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    for (const MpvWidget::Track &track : tracks) {
        if (track.type == QLatin1String("audio"))
            m_audioTracks.append(track);
        else if (track.type == QLatin1String("sub"))
            m_subtitleTracks.append(track);
    }
    m_audioTrackButton->setEnabled(!m_audioTracks.isEmpty());
    m_subtitleTrackButton->setEnabled(!m_subtitleTracks.isEmpty());
}

void PlayerWindow::onVolumeSliderChanged(int value)
{
    m_mpv->setVolume(value);
    if (value > 0 && m_mpv->isMuted())
        m_mpv->setMuted(false);
    updateMuteIcon();
}

void PlayerWindow::onMuteClicked()
{
    m_mpv->setMuted(!m_mpv->isMuted());
    updateMuteIcon();
}

void PlayerWindow::showAudioTrackMenu()
{
    QMenu menu(this);
    for (const MpvWidget::Track &track : std::as_const(m_audioTracks)) {
        QAction *action = menu.addAction(trackLabel(track));
        action->setCheckable(true);
        action->setChecked(track.selected);
        const int id = track.id;
        connect(action, &QAction::triggered, this, [this, id]() { m_mpv->setAudioTrack(id); });
    }
    menu.exec(m_audioTrackButton->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void PlayerWindow::showSubtitleTrackMenu()
{
    QMenu menu(this);
    QAction *none = menu.addAction(QStringLiteral("None"));
    none->setCheckable(true);
    bool anySelected = false;
    for (const MpvWidget::Track &track : std::as_const(m_subtitleTracks))
        anySelected = anySelected || track.selected;
    none->setChecked(!anySelected);
    connect(none, &QAction::triggered, this, [this]() { m_mpv->setSubtitleTrack(-1); });

    for (const MpvWidget::Track &track : std::as_const(m_subtitleTracks)) {
        QAction *action = menu.addAction(trackLabel(track));
        action->setCheckable(true);
        action->setChecked(track.selected);
        const int id = track.id;
        connect(action, &QAction::triggered, this, [this, id]() { m_mpv->setSubtitleTrack(id); });
    }
    menu.exec(m_subtitleTrackButton->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
}

void PlayerWindow::toggleFullscreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void PlayerWindow::revealControls()
{
    setCursor(Qt::ArrowCursor);
    m_hideTimer->start();

    if (m_overlay->isVisible() && qFuzzyCompare(m_overlayOpacity->opacity(), 1.0))
        return;

    m_overlay->show();
    m_overlayAnimation->stop();
    m_overlayAnimation->setStartValue(m_overlayOpacity->opacity());
    m_overlayAnimation->setEndValue(1.0);
    m_overlayAnimation->start();
}

void PlayerWindow::hideControls()
{
    setCursor(Qt::BlankCursor);
    m_overlayAnimation->stop();
    m_overlayAnimation->setStartValue(m_overlayOpacity->opacity());
    m_overlayAnimation->setEndValue(0.0);
    m_overlayAnimation->start();
}

void PlayerWindow::updatePlayPauseIcon()
{
    const QStyle::StandardPixmap sp = m_mpv->isPaused() ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause;
    m_playPauseButton->setIcon(style()->standardIcon(sp));
    m_playPauseButton->setToolTip(m_mpv->isPaused() ? QStringLiteral("Play (Space)") : QStringLiteral("Pause (Space)"));
}

void PlayerWindow::updateMuteIcon()
{
    const bool muted = m_mpv->isMuted() || m_volumeSlider->value() == 0;
    const QStyle::StandardPixmap sp = muted ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume;
    m_muteButton->setIcon(style()->standardIcon(sp));
    m_muteButton->setToolTip(QStringLiteral("Mute (M)"));
}

void PlayerWindow::seekRelativeSeconds(qint64 delta)
{
    m_mpv->seekRelative(delta);
}

void PlayerWindow::adjustVolume(int delta)
{
    const int value = qBound(0, m_volumeSlider->value() + delta, 100);
    m_volumeSlider->setValue(value);
}

void PlayerWindow::keyPressEvent(QKeyEvent *event)
{
    revealControls();

    switch (event->key()) {
    case Qt::Key_Space:
        m_mpv->togglePause();
        break;
    case Qt::Key_Left:
        seekRelativeSeconds(event->modifiers() & Qt::ShiftModifier ? -60 : -5);
        break;
    case Qt::Key_Right:
        seekRelativeSeconds(event->modifiers() & Qt::ShiftModifier ? 60 : 5);
        break;
    case Qt::Key_J:
        seekRelativeSeconds(-10);
        break;
    case Qt::Key_K:
        m_mpv->togglePause();
        break;
    case Qt::Key_L:
        seekRelativeSeconds(10);
        break;
    case Qt::Key_Up:
        adjustVolume(5);
        break;
    case Qt::Key_Down:
        adjustVolume(-5);
        break;
    case Qt::Key_M:
        onMuteClicked();
        break;
    case Qt::Key_F:
        toggleFullscreen();
        break;
    case Qt::Key_I:
        // mpv's own convention for the stats panel; Ctrl+Shift+D is handled
        // below for people who expect a devtools-style binding.
        toggleStatsOverlay();
        break;
    case Qt::Key_D:
        if (event->modifiers().testFlag(Qt::ControlModifier)
            && event->modifiers().testFlag(Qt::ShiftModifier)) {
            toggleStatsOverlay();
            break;
        }
        QWidget::keyPressEvent(event);
        return;
    case Qt::Key_C:
        if (event->modifiers().testFlag(Qt::ControlModifier)
            && event->modifiers().testFlag(Qt::ShiftModifier)) {
            copyDiagnostics();
            break;
        }
        QWidget::keyPressEvent(event);
        return;
    case Qt::Key_PageUp:
        jumpChapter(-1);
        break;
    case Qt::Key_PageDown:
        jumpChapter(1);
        break;
    case Qt::Key_BracketLeft:
        adjustSpeed(1.0 / 1.25);
        break;
    case Qt::Key_BracketRight:
        adjustSpeed(1.25);
        break;
    case Qt::Key_Backspace:
        m_mpv->setSpeed(1.0);
        break;
    case Qt::Key_Escape:
        if (isFullScreen())
            showNormal();
        else
            close();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void PlayerWindow::mouseMoveEvent(QMouseEvent *event)
{
    revealControls();
    QWidget::mouseMoveEvent(event);
}

void PlayerWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    toggleFullscreen();
    QWidget::mouseDoubleClickEvent(event);
}

bool PlayerWindow::eventFilter(QObject *watched, QEvent *event)
{
    // The seek bar used to get a plain timestamp tooltip from here; SeekBar
    // now owns its own hover preview (timestamp plus a trickplay thumbnail
    // when the server has one), so this only keeps the controls awake.
    if (event->type() == QEvent::MouseMove)
        revealControls();
    return QWidget::eventFilter(watched, event);
}

void PlayerWindow::closeEvent(QCloseEvent *event)
{
    if (m_reporter)
        m_reporter->stop();
    m_mpv->stop();
    QWidget::closeEvent(event);
}
