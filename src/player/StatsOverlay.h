#pragma once

#include <QString>
#include <QWidget>

class MpvWidget;
class QTimer;

// The mpv-stats-style diagnostics panel (FEATURES.md §C3), toggled with `I`
// or Ctrl+Shift+D.
//
// Polls mpv properties at 2Hz and *only while visible* -- a hidden overlay
// must cost exactly nothing, which is why the timer is started/stopped in
// showEvent/hideEvent rather than running for the player's lifetime.
// Everything is drawn as monospace text in palette-derived colours over a
// palette().shadow() scrim.
class StatsOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit StatsOverlay(MpvWidget *mpv, QWidget *parent = nullptr);

    // Jellyfin-side context the overlay cannot get from mpv: whether the
    // server chose to transcode and why, plus the session identifiers that
    // make a bug report actionable.
    void setPlaybackContext(const QString &playSessionId, const QString &mediaSourceId, bool transcoding,
                             const QStringList &transcodeReasons, const QString &serverName);

    // Appends one mpv log line to the tail buffer (kept to kMaxLogLines).
    void appendLogLine(const QString &line);

    // The whole panel as plain text, for the "copy diagnostics" action --
    // this is the single most useful thing a user can hand over when a file
    // misbehaves, so it must be one keystroke away.
    QString diagnosticsText() const;

    static constexpr int kMaxLogLines = 20;
    static constexpr int kPollIntervalMs = 500;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct Row {
        QString label;
        QString value;
    };
    void refresh();
    QList<Row> collectRows() const;

    MpvWidget *m_mpv;
    QTimer *m_timer;

    QString m_playSessionId;
    QString m_mediaSourceId;
    bool m_transcoding = false;
    QStringList m_transcodeReasons;
    QString m_serverName;

    QStringList m_logLines;
    QList<Row> m_rows;
};
