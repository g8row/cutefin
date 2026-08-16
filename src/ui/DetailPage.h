#pragma once

#include "api/Models.h"

#include <QList>
#include <QWidget>

class AsyncImage;
class ImageCache;
class JellyfinClient;
class MediaRow;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class SmoothScrollArea;

// Item detail: a dimmed backdrop behind a poster + metadata block (title,
// year, runtime, rating chips, genres, tagline, overview) with a primary
// "Play" button and a "Resume from 12:34" button when there is a stored
// position. Series get a season selector and an episode list; episodes show
// their series/season context.
//
// setItem() paints immediately from whatever the caller already has (so
// navigation is instant) and then refetches the full item to fill in fields
// list endpoints omit -- overview, people, genres.
class DetailPage : public QWidget
{
    Q_OBJECT
public:
    DetailPage(JellyfinClient *client, ImageCache *imageCache, QWidget *parent = nullptr);

    void setItem(const MediaItem &item);
    const MediaItem &item() const { return m_item; }

signals:
    // `resume` requests playback from item.userData.playbackPositionTicks.
    void playRequested(const MediaItem &item, bool resume);
    void itemActivated(const MediaItem &item);
    void backRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildFromItem();
    void updateActionButtons();
    void loadSeasons();
    void loadEpisodes(const QString &seasonId);
    void onSeasonChanged(int index);
    void togglePlayed();
    void toggleFavorite();

    JellyfinClient *m_client;
    ImageCache *m_imageCache;
    MediaItem m_item;

    AsyncImage *m_backdrop;
    AsyncImage *m_poster;
    QLabel *m_titleLabel;
    QLabel *m_subtitleLabel;   // year · runtime · rating chips
    QLabel *m_genresLabel;
    QLabel *m_taglineLabel;
    QLabel *m_overviewLabel;

    QPushButton *m_playButton;
    QPushButton *m_resumeButton;
    QPushButton *m_playedButton;
    QPushButton *m_favoriteButton;
    QPushButton *m_backButton;

    QWidget *m_seasonBar;
    QComboBox *m_seasonCombo;
    QWidget *m_episodeSection;
    QVBoxLayout *m_episodeLayout;
    MediaRow *m_castRow;

    SmoothScrollArea *m_scrollArea;
    QWidget *m_content;

    QList<MediaItem> m_seasons;
    quint64 m_generation = 0;
};
