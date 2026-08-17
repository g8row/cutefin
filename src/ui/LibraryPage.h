#pragma once

#include "api/JellyfinClient.h"
#include "api/Models.h"

#include <QSet>
#include <QWidget>

class ImageCache;
class PosterGrid;
class SmoothScrollArea;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;

// A browsable view over one library: a header (title, result count, sort
// selector, filter menu) above a virtualized PosterGrid.
//
// It owns the current ItemQuery. Changing sort or a filter re-runs the query
// from index 0; scrolling asks the grid for more pages. Pages already in
// flight are tracked in m_pagesInFlight so an overscan that crosses the same
// boundary twice does not issue duplicate requests.
//
// Also serves person filmographies and genre-filtered views: those are just
// an ItemQuery with personId or genres preset, so they need no separate page.
class LibraryPage : public QWidget
{
    Q_OBJECT
public:
    LibraryPage(JellyfinClient *client, ImageCache *imageCache, QWidget *parent = nullptr);

    // Browses a library view (a "My Media" tile). Derives the item types and
    // default sort from the view's collectionType.
    void showLibrary(const MediaItem &view);

    // Browses an arbitrary query with an explicit title -- used for person
    // filmographies ("Films with <name>") and genre chips ("Horror").
    void showQuery(const QString &title, const JellyfinClient::ItemQuery &query);

    void focusGrid();

signals:
    void itemActivated(const MediaItem &item);
    void itemContextMenuRequested(const MediaItem &item, const QPoint &globalPos);
    void backRequested();

private:
    void runQuery();                       // resets the grid and fetches page 0
    void fetchPage(int startIndex, int count);
    void buildSortMenu();
    void showFilterMenu();
    void loadFilters();
    void applySort(int comboIndex);
    void updateHeader();
    void updateFilterButtonState();

    JellyfinClient *m_client;
    ImageCache *m_imageCache;

    QLabel *m_titleLabel;
    QLabel *m_countLabel;
    QComboBox *m_sortCombo;
    QToolButton *m_sortDirectionButton;
    QPushButton *m_filterButton;
    QPushButton *m_backButton;
    QLabel *m_emptyLabel;

    SmoothScrollArea *m_scrollArea;
    PosterGrid *m_grid;

    QString m_title;
    JellyfinClient::ItemQuery m_query;
    JellyfinClient::LibraryFilters m_available;
    bool m_filtersLoaded = false;

    QSet<int> m_pagesInFlight;
    // Bumped per query; in-flight page callbacks from a superseded query
    // compare against it and drop their results.
    quint64 m_generation = 0;
};
