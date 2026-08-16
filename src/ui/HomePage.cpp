#include "HomePage.h"

#include "api/JellyfinClient.h"
#include "core/ImageCache.h"
#include "widgets/MediaRow.h"
#include "widgets/PosterCard.h"
#include "widgets/SmoothScrollArea.h"

#include <QAction>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kSearchDebounceMs = 250;
}

HomePage::HomePage(JellyfinClient *client, ImageCache *imageCache, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
    , m_imageCache(imageCache)
{
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setStyleSheet(QStringLiteral("QLineEdit { padding: 6px 8px; border-radius: 6px; }"));

    m_userButton = new QToolButton(this);
    m_userButton->setPopupMode(QToolButton::InstantPopup);
    m_userButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QMenu *userMenu = new QMenu(m_userButton);
    QAction *logoutAction = userMenu->addAction(tr("Log out"));
    connect(logoutAction, &QAction::triggered, this, &HomePage::logoutRequested);
    m_userButton->setMenu(userMenu);

    QHBoxLayout *topBar = new QHBoxLayout;
    topBar->addWidget(m_searchEdit);
    topBar->addStretch();
    topBar->addWidget(m_userButton);

    m_column = new QWidget;
    m_columnLayout = new QVBoxLayout(m_column);
    m_columnLayout->setContentsMargins(0, 0, 0, 0);
    m_columnLayout->setSpacing(8);

    m_emptyLabel = new QLabel(tr("Nothing here yet"), m_column);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setForegroundRole(QPalette::PlaceholderText);
    m_emptyLabel->hide();
    m_columnLayout->addWidget(m_emptyLabel);
    // Keeps rows stacked from the top; addRow() always inserts before this.
    m_columnLayout->addStretch();

    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidget(m_column);

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->addLayout(topBar);
    outer->addWidget(m_scrollArea, 1);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(kSearchDebounceMs);
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() {
        const QString query = m_searchEdit->text().trimmed();
        if (query.isEmpty())
            showHomeRows();
        else
            runSearch(query);
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &) { m_searchDebounce->start(); });
}

void HomePage::refresh()
{
    // Stale in-flight callbacks from the previous generation compare against
    // this and bail instead of touching rows that clearRows() just deleted.
    ++m_generation;
    clearRows();
    m_emptyLabel->hide();
    loadHomeRows();
}

void HomePage::setUserName(const QString &userName)
{
    m_userName = userName;
    m_userButton->setText(m_userName);
}

void HomePage::focusSearch()
{
    m_searchEdit->setFocus(Qt::ShortcutFocusReason);
    m_searchEdit->selectAll();
}

MediaRow *HomePage::addRow(const QString &title)
{
    MediaRow *row = new MediaRow(title, m_column);
    row->setImageCache(m_imageCache);
    connect(row, &MediaRow::itemActivated, this, &HomePage::itemActivated);
    connect(row, &MediaRow::itemContextMenuRequested, this, &HomePage::itemContextMenuRequested);
    // Insert before the trailing stretch so rows keep stacking from the top.
    m_columnLayout->insertWidget(m_columnLayout->count() - 1, row);
    // Hidden until setItems() decides whether it has content -- avoids a
    // flash of an empty titled row while its fetch is still in flight.
    row->hide();
    return row;
}

void HomePage::clearRows()
{
    for (MediaRow *row : std::as_const(m_homeRows)) {
        row->hide();
        row->deleteLater();
    }
    m_homeRows.clear();
}

void HomePage::loadHomeRows()
{
    const quint64 generation = m_generation;

    MediaRow *myMedia = addRow(tr("My Media"));
    m_homeRows.append(myMedia);
    MediaRow *continueWatching = addRow(tr("Continue Watching"));
    m_homeRows.append(continueWatching);
    MediaRow *nextUp = addRow(tr("Next Up"));
    m_homeRows.append(nextUp);

    m_client->fetchUserViews(
        this, [this, generation, myMedia](bool ok, const QList<MediaItem> &items, const QString &error) {
            if (generation != m_generation)
                return; // a newer refresh() already cleared this row away

            if (!ok) {
                qWarning() << "HomePage: fetchUserViews failed:" << error;
                myMedia->setItems({});
                updateEmptyState();
                return;
            }

            myMedia->setItems(items, PosterCard::Shape::Thumb);
            updateEmptyState();

            // Create every "Latest in <view>" row up front, in view order, so
            // their final position is stable no matter which fetchLatestItems
            // reply lands first -- only fill each one from its own callback.
            for (const MediaItem &view : items) {
                if (view.collectionType != QStringLiteral("movies")
                    && view.collectionType != QStringLiteral("tvshows")
                    && view.collectionType != QStringLiteral("boxsets"))
                    continue;

                MediaRow *latestRow = addRow(tr("Latest in %1").arg(view.name));
                m_homeRows.append(latestRow);

                m_client->fetchLatestItems(
                    view.id, this,
                    [this, generation, latestRow](bool latestOk, const QList<MediaItem> &latestItems,
                                                   const QString &latestError) {
                        if (generation != m_generation)
                            return;

                        if (!latestOk) {
                            qWarning() << "HomePage: fetchLatestItems failed:" << latestError;
                            latestRow->setItems({});
                            updateEmptyState();
                            return;
                        }

                        latestRow->setItems(latestItems, PosterCard::Shape::Poster);
                        updateEmptyState();
                    });
            }
        });

    m_client->fetchResumeItems(
        this, [this, generation, continueWatching](bool ok, const QList<MediaItem> &items, const QString &error) {
            if (generation != m_generation)
                return;

            if (!ok) {
                qWarning() << "HomePage: fetchResumeItems failed:" << error;
                continueWatching->setItems({});
                updateEmptyState();
                return;
            }

            continueWatching->setItems(items, PosterCard::Shape::Thumb);
            updateEmptyState();
        });

    m_client->fetchNextUp(
        this, [this, generation, nextUp](bool ok, const QList<MediaItem> &items, const QString &error) {
            if (generation != m_generation)
                return;

            if (!ok) {
                qWarning() << "HomePage: fetchNextUp failed:" << error;
                nextUp->setItems({});
                updateEmptyState();
                return;
            }

            nextUp->setItems(items, PosterCard::Shape::Thumb);
            updateEmptyState();
        });
}

void HomePage::runSearch(const QString &query)
{
    const quint64 generation = m_generation;
    m_client->searchItems(query, this,
                           [this, generation](bool ok, const QList<MediaItem> &items, const QString &error) {
                               if (generation != m_generation)
                                   return;

                               if (!ok) {
                                   qWarning() << "HomePage: searchItems failed:" << error;
                                   showSearchResults({});
                                   return;
                               }

                               showSearchResults(items);
                           });
}

void HomePage::showSearchResults(const QList<MediaItem> &items)
{
    // Rebuilt from scratch every time rather than reused across searches --
    // simpler than diffing three groups against a shrinking/growing result set.
    for (MediaRow *row : std::as_const(m_searchRows)) {
        row->hide();
        row->deleteLater();
    }
    m_searchRows.clear();

    QList<MediaItem> movies, series, episodes;
    for (const MediaItem &item : items) {
        if (item.type == QStringLiteral("Movie"))
            movies.append(item);
        else if (item.type == QStringLiteral("Series"))
            series.append(item);
        else if (item.type == QStringLiteral("Episode"))
            episodes.append(item);
    }

    MediaRow *moviesRow = addRow(tr("Movies"));
    moviesRow->setItems(movies, PosterCard::Shape::Poster);
    m_searchRows.append(moviesRow);

    MediaRow *seriesRow = addRow(tr("Series"));
    seriesRow->setItems(series, PosterCard::Shape::Poster);
    m_searchRows.append(seriesRow);

    MediaRow *episodesRow = addRow(tr("Episodes"));
    episodesRow->setItems(episodes, PosterCard::Shape::Thumb);
    m_searchRows.append(episodesRow);

    m_searchActive = true;
    for (MediaRow *row : std::as_const(m_homeRows))
        row->hide();

    updateEmptyState();
}

void HomePage::showHomeRows()
{
    m_searchActive = false;

    for (MediaRow *row : std::as_const(m_searchRows)) {
        row->hide();
        row->deleteLater();
    }
    m_searchRows.clear();

    // Restore each row to whatever its last fetch decided -- no refetch.
    for (MediaRow *row : std::as_const(m_homeRows))
        row->setVisible(!row->isEmpty());

    updateEmptyState();
}

void HomePage::updateEmptyState()
{
    const QList<MediaRow *> &rows = m_searchActive ? m_searchRows : m_homeRows;

    bool anyContent = false;
    for (MediaRow *row : rows) {
        if (!row->isEmpty()) {
            anyContent = true;
            break;
        }
    }
    m_emptyLabel->setVisible(!anyContent);
}
