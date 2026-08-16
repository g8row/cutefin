#pragma once

#include "player/MpvWidget.h"

#include <QList>
#include <QString>
#include <QUrl>
#include <QWidget>

class JellyfinClient;
class PlaybackReporter;
class QCloseEvent;
class QGraphicsOpacityEffect;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QPropertyAnimation;
class QResizeEvent;
class QSlider;
class QTimer;
class QToolButton;

// Hosts an MpvWidget with a controls overlay: play/pause, seek slider,
// volume, audio/subtitle track menus, fullscreen toggle. Controls auto-hide
// after 2.5s of mouse stillness (DESIGN.md §7).
//
// `client` may be null: PlayerWindow can be driven purely through playUrl()
// (used by the `cutefin --play` debug entry point in main.cpp) without a
// JellyfinClient/session, in which case playback progress is simply not
// reported to a server.
class PlayerWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PlayerWindow(JellyfinClient *client, QWidget *parent = nullptr);
    ~PlayerWindow() override;

    // Full playback-resolution entry point: reports Sessions/Playing* for
    // the given item alongside playback.
    void playItem(const QString &itemId, const QString &playSessionId, const QString &mediaSourceId,
                  const QUrl &url, qint64 startSeconds = 0, const QStringList &httpHeaderFields = {});

    // Minimal entry point with no server-side progress reporting; used by
    // the standalone debug harness (`cutefin --play <url>`).
    void playUrl(const QUrl &url, qint64 startSeconds = 0, const QString &title = {});

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onPlayPauseClicked();
    void onSeekSliderPressed();
    void onSeekSliderReleased();
    void onSeekSliderMoved(int value);
    void onPositionChanged(qint64 seconds);
    void onDurationChanged(qint64 seconds);
    void onPausedChanged(bool paused);
    void onTracksChanged(const QList<MpvWidget::Track> &tracks);
    void onVolumeSliderChanged(int value);
    void onMuteClicked();
    void showAudioTrackMenu();
    void showSubtitleTrackMenu();
    void toggleFullscreen();
    void revealControls();
    void hideControls();

private:
    void buildUi();
    void layoutOverlay();
    void updatePlayPauseIcon();
    void updateMuteIcon();
    void seekRelativeSeconds(qint64 delta);
    void adjustVolume(int delta);

    MpvWidget *m_mpv;
    JellyfinClient *m_client;
    PlaybackReporter *m_reporter = nullptr;

    QWidget *m_overlay;
    QToolButton *m_playPauseButton;
    QSlider *m_seekSlider;
    QLabel *m_positionLabel;
    QLabel *m_durationLabel;
    QToolButton *m_muteButton;
    QSlider *m_volumeSlider;
    QToolButton *m_audioTrackButton;
    QToolButton *m_subtitleTrackButton;
    QToolButton *m_fullscreenButton;

    QGraphicsOpacityEffect *m_overlayOpacity;
    QPropertyAnimation *m_overlayAnimation;
    QTimer *m_hideTimer;

    QList<MpvWidget::Track> m_audioTracks;
    QList<MpvWidget::Track> m_subtitleTracks;

    bool m_seekSliderDragging = false;
    qint64 m_durationSeconds = 0;
};
