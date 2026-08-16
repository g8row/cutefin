#pragma once

#include "api/Models.h"

#include <QList>
#include <QWidget>

class ImageCache;
class JellyfinClient;
class MediaRow;
class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;
class QVBoxLayout;
class SmoothScrollArea;

// The landing page after login: a top bar (search field + user menu) over a
// vertically scrolling column of MediaRows -- My Media, Continue Watching,
// Next Up, then "Latest in <library>" for each user view. A row whose fetch
// resolves empty hides itself.
//
// Typing in the search field switches the column to grouped search results
// after a 250ms debounce (FEATURES.md C1); clearing it restores the home
// rows without refetching them.
class HomePage : public QWidget
{
    Q_OBJECT
public:
    HomePage(JellyfinClient *client, ImageCache *imageCache, QWidget *parent = nullptr);

    // (Re)fetches every row. Safe to call repeatedly -- e.g. after returning
    // from the player so Continue Watching reflects new progress.
    void refresh();

    void setUserName(const QString &userName);

    // Moves keyboard focus into the search field ("/" shortcut, C6).
    void focusSearch();

signals:
    void itemActivated(const MediaItem &item);
    void itemContextMenuRequested(const MediaItem &item, const QPoint &globalPos);
    void logoutRequested();

private:
    MediaRow *addRow(const QString &title);
    void clearRows();
    void loadHomeRows();
    void runSearch(const QString &query);
    void showSearchResults(const QList<MediaItem> &items);
    void showHomeRows();
    void updateEmptyState();

    JellyfinClient *m_client;
    ImageCache *m_imageCache;

    QLineEdit *m_searchEdit;
    QToolButton *m_userButton;
    SmoothScrollArea *m_scrollArea;
    QWidget *m_column;
    QVBoxLayout *m_columnLayout;
    QLabel *m_emptyLabel;

    QList<MediaRow *> m_homeRows;
    QList<MediaRow *> m_searchRows;
    QTimer *m_searchDebounce;
    QString m_userName;
    bool m_searchActive = false;
    // Bumped on every refresh(); in-flight callbacks from an older refresh
    // compare against it and drop their results instead of appending rows to
    // a column that has since been cleared.
    quint64 m_generation = 0;
};
