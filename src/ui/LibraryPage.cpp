#include "LibraryPage.h"

#include "core/ImageCache.h"
#include "widgets/PosterGrid.h"
#include "widgets/SmoothScrollArea.h"

#include <QAction>
#include <QComboBox>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPair>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

using ItemQuery = JellyfinClient::ItemQuery;

// Sort combo entries: user-facing label -> Jellyfin sortBy value, all
// verified against the live 10.11.8 server (DESIGN.md's brief for this
// page). Order here is the order they appear in the combo.
const QList<QPair<QString, QString>> kSortOptions = {
    {QStringLiteral("Name"), QStringLiteral("SortName")},
    {QStringLiteral("Date Added"), QStringLiteral("DateCreated")},
    {QStringLiteral("Release Date"), QStringLiteral("PremiereDate")},
    {QStringLiteral("Community Rating"), QStringLiteral("CommunityRating")},
    {QStringLiteral("Runtime"), QStringLiteral("Runtime")},
    {QStringLiteral("Date Played"), QStringLiteral("DatePlayed")},
    {QStringLiteral("Random"), QStringLiteral("Random")},
};

} // namespace

LibraryPage::LibraryPage(JellyfinClient *client, ImageCache *imageCache, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
    , m_imageCache(imageCache)
{
    m_backButton = new QPushButton(tr("Back"), this);
    connect(m_backButton, &QPushButton::clicked, this, &LibraryPage::backRequested);

    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_countLabel = new QLabel(this);
    m_countLabel->setForegroundRole(QPalette::PlaceholderText);

    m_sortCombo = new QComboBox(this);
    buildSortMenu();
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, &LibraryPage::applySort);

    m_sortDirectionButton = new QToolButton(this);
    m_sortDirectionButton->setCheckable(true);
    m_sortDirectionButton->setToolTip(tr("Sort direction"));
    connect(m_sortDirectionButton, &QToolButton::toggled, this, [this](bool) { applySort(m_sortCombo->currentIndex()); });

    m_filterButton = new QPushButton(tr("Filter"), this);
    m_filterButton->setCheckable(true);
    connect(m_filterButton, &QPushButton::clicked, this, &LibraryPage::showFilterMenu);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->addWidget(m_backButton);
    headerLayout->addSpacing(12);
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addWidget(m_countLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_sortCombo);
    headerLayout->addWidget(m_sortDirectionButton);
    headerLayout->addWidget(m_filterButton);

    m_grid = new PosterGrid(this);
    m_grid->setImageCache(m_imageCache);
    connect(m_grid, &PosterGrid::pageNeeded, this, &LibraryPage::fetchPage);
    connect(m_grid, &PosterGrid::itemActivated, this, &LibraryPage::itemActivated);
    connect(m_grid, &PosterGrid::itemContextMenuRequested, this, &LibraryPage::itemContextMenuRequested);

    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidget(m_grid);
    m_grid->setScrollArea(m_scrollArea);

    m_emptyLabel = new QLabel(tr("No items found"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setForegroundRole(QPalette::PlaceholderText);
    QFont emptyFont = m_emptyLabel->font();
    emptyFont.setPointSize(emptyFont.pointSize() + 2);
    m_emptyLabel->setFont(emptyFont);
    m_emptyLabel->hide();

    auto *bodyLayout = new QVBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->addWidget(m_scrollArea, 1);
    bodyLayout->addWidget(m_emptyLabel, 1);

    auto *outer = new QVBoxLayout(this);
    outer->addLayout(headerLayout);
    outer->addLayout(bodyLayout, 1);

    updateFilterButtonState();
}

void LibraryPage::showLibrary(const MediaItem &view)
{
    m_title = view.name;

    ItemQuery query;
    query.parentId = view.id;
    if (view.collectionType == QStringLiteral("movies"))
        query.includeItemTypes = {QStringLiteral("Movie")};
    else if (view.collectionType == QStringLiteral("tvshows"))
        query.includeItemTypes = {QStringLiteral("Series")};
    else if (view.collectionType == QStringLiteral("boxsets"))
        query.includeItemTypes = {QStringLiteral("BoxSet")};
    // else: leave includeItemTypes empty -- browse everything under parentId.

    m_query = query;
    m_available = JellyfinClient::LibraryFilters();
    m_filtersLoaded = false;
    m_filterButton->setEnabled(true);

    // Reset sort UI to the default (Name, ascending) without re-triggering
    // applySort() per signal -- runQuery() below is the single refetch.
    m_sortCombo->blockSignals(true);
    m_sortCombo->setCurrentIndex(0);
    m_sortCombo->blockSignals(false);
    m_sortDirectionButton->blockSignals(true);
    m_sortDirectionButton->setChecked(false);
    m_sortDirectionButton->blockSignals(false);
    m_sortDirectionButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));

    updateHeader();
    updateFilterButtonState();
    runQuery();
    loadFilters();
}

void LibraryPage::showQuery(const QString &title, const JellyfinClient::ItemQuery &query)
{
    m_title = title;
    m_query = query;

    // A person filmography / genre chip view has no meaningful per-library
    // filter set (Filters2 is keyed by parentId, which these queries don't
    // necessarily have in the same sense) -- hide the filter affordance
    // rather than show a menu that would always come back empty.
    m_available = JellyfinClient::LibraryFilters();
    m_filtersLoaded = false;
    m_filterButton->setEnabled(false);

    // Keep the sort controls truthful even though this query didn't come from
    // them -- otherwise toggling direction afterwards would silently jump to
    // whatever the combo last showed instead of what's actually applied.
    m_sortCombo->blockSignals(true);
    const int sortIndex = m_sortCombo->findData(query.sortBy);
    m_sortCombo->setCurrentIndex(sortIndex >= 0 ? sortIndex : 0);
    m_sortCombo->blockSignals(false);
    m_sortDirectionButton->blockSignals(true);
    m_sortDirectionButton->setChecked(!query.ascending);
    m_sortDirectionButton->blockSignals(false);
    m_sortDirectionButton->setIcon(style()->standardIcon(query.ascending ? QStyle::SP_ArrowUp : QStyle::SP_ArrowDown));

    updateHeader();
    updateFilterButtonState();
    runQuery();
}

void LibraryPage::focusGrid()
{
    m_grid->setFocus(Qt::ShortcutFocusReason);
}

void LibraryPage::runQuery()
{
    // Every in-flight page callback from the previous query compares against
    // this and drops its result -- otherwise a page fetched under the old
    // sort/filter can land in the grid after it has already been reset for
    // the new one.
    ++m_generation;
    m_pagesInFlight.clear();

    m_query.startIndex = 0;
    m_query.limit = 100;

    updateHeader();
    fetchPage(0, 100);
}

void LibraryPage::fetchPage(int startIndex, int count)
{
    if (m_pagesInFlight.contains(startIndex))
        return;
    m_pagesInFlight.insert(startIndex);

    const quint64 generation = m_generation;
    const bool isFirstPage = (startIndex == 0);

    ItemQuery q = m_query;
    q.startIndex = startIndex;
    q.limit = count;

    m_client->fetchItems(
        q, this, [this, startIndex, generation, isFirstPage](bool ok, const JellyfinClient::ItemPage &page,
                                                               const QString &error) {
            if (generation != m_generation) {
                // Superseded by a newer sort/filter/query. Deliberately do NOT
                // touch m_pagesInFlight: runQuery() already cleared it, and the
                // new query may have re-inserted this same startIndex for a
                // request that is still genuinely in flight -- clearing it here
                // would drop the de-dup guard and let a duplicate fetch through.
                return;
            }
            m_pagesInFlight.remove(startIndex);

            if (!ok) {
                qWarning() << "LibraryPage: fetchItems failed:" << error;
                if (isFirstPage) {
                    m_grid->reset(0);
                    m_countLabel->clear();
                    m_emptyLabel->setText(tr("Couldn't load this library"));
                    m_emptyLabel->show();
                    m_scrollArea->hide();
                }
                return;
            }

            if (isFirstPage) {
                m_grid->reset(page.totalRecordCount);
                m_countLabel->setText(tr("%n item(s)", "library result count", page.totalRecordCount));
                const bool empty = page.totalRecordCount == 0;
                m_emptyLabel->setText(tr("No items found"));
                m_emptyLabel->setVisible(empty);
                m_scrollArea->setVisible(!empty);
            }

            m_grid->setItems(page.startIndex, page.items);
        });
}

void LibraryPage::buildSortMenu()
{
    m_sortCombo->clear();
    for (const auto &option : kSortOptions)
        m_sortCombo->addItem(option.first, option.second);
}

void LibraryPage::showFilterMenu()
{
    if (!m_filterButton->isEnabled()) {
        m_filterButton->setChecked(false);
        return;
    }

    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // Reopening the menu after each toggle (rather than fighting QMenu to
    // keep a submenu open across an action trigger) keeps this simple and
    // matches how native filter menus already behave on both platforms.
    // Each toggle handler below already calls updateFilterButtonState(); this
    // just guarantees the button reflects final state once the menu closes.
    connect(menu, &QMenu::aboutToHide, this, &LibraryPage::updateFilterButtonState);

    if (!m_available.genres.isEmpty()) {
        QMenu *genreMenu = menu->addMenu(tr("Genre"));
        for (const QString &genre : m_available.genres) {
            QAction *action = genreMenu->addAction(genre);
            action->setCheckable(true);
            action->setChecked(m_query.genres.contains(genre));
            connect(action, &QAction::toggled, this, [this, genre](bool checked) {
                if (checked) {
                    if (!m_query.genres.contains(genre))
                        m_query.genres.append(genre);
                } else {
                    m_query.genres.removeAll(genre);
                }
                updateFilterButtonState();
                runQuery();
            });
        }
    }

    if (!m_available.years.isEmpty()) {
        QMenu *yearMenu = menu->addMenu(tr("Year"));
        for (int year : m_available.years) {
            QAction *action = yearMenu->addAction(QString::number(year));
            action->setCheckable(true);
            action->setChecked(m_query.years.contains(year));
            connect(action, &QAction::toggled, this, [this, year](bool checked) {
                if (checked) {
                    if (!m_query.years.contains(year))
                        m_query.years.append(year);
                } else {
                    m_query.years.removeAll(year);
                }
                updateFilterButtonState();
                runQuery();
            });
        }
    }

    if (!m_available.officialRatings.isEmpty()) {
        QMenu *ratingMenu = menu->addMenu(tr("Rating"));
        for (const QString &rating : m_available.officialRatings) {
            QAction *action = ratingMenu->addAction(rating);
            action->setCheckable(true);
            action->setChecked(m_query.officialRatings.contains(rating));
            connect(action, &QAction::toggled, this, [this, rating](bool checked) {
                if (checked) {
                    if (!m_query.officialRatings.contains(rating))
                        m_query.officialRatings.append(rating);
                } else {
                    m_query.officialRatings.removeAll(rating);
                }
                updateFilterButtonState();
                runQuery();
            });
        }
    }

    menu->addSeparator();

    QAction *unplayedAction = menu->addAction(tr("Unplayed only"));
    unplayedAction->setCheckable(true);
    unplayedAction->setChecked(m_query.played == ItemQuery::Played::UnplayedOnly);
    connect(unplayedAction, &QAction::toggled, this, [this](bool checked) {
        m_query.played = checked ? ItemQuery::Played::UnplayedOnly : ItemQuery::Played::Any;
        updateFilterButtonState();
        runQuery();
    });

    QAction *playedAction = menu->addAction(tr("Played only"));
    playedAction->setCheckable(true);
    playedAction->setChecked(m_query.played == ItemQuery::Played::PlayedOnly);
    connect(playedAction, &QAction::toggled, this, [this](bool checked) {
        m_query.played = checked ? ItemQuery::Played::PlayedOnly : ItemQuery::Played::Any;
        updateFilterButtonState();
        runQuery();
    });

    QAction *favoritesAction = menu->addAction(tr("Favourites only"));
    favoritesAction->setCheckable(true);
    favoritesAction->setChecked(m_query.favoritesOnly);
    connect(favoritesAction, &QAction::toggled, this, [this](bool checked) {
        m_query.favoritesOnly = checked;
        updateFilterButtonState();
        runQuery();
    });

    const bool anyFilterActive = !m_query.genres.isEmpty() || !m_query.officialRatings.isEmpty()
        || !m_query.years.isEmpty() || m_query.played != ItemQuery::Played::Any || m_query.favoritesOnly;
    if (anyFilterActive) {
        menu->addSeparator();
        QAction *clearAction = menu->addAction(tr("Clear filters"));
        connect(clearAction, &QAction::triggered, this, [this]() {
            m_query.genres.clear();
            m_query.officialRatings.clear();
            m_query.years.clear();
            m_query.played = ItemQuery::Played::Any;
            m_query.favoritesOnly = false;
            updateFilterButtonState();
            runQuery();
        });
    }

    menu->popup(m_filterButton->mapToGlobal(QPoint(0, m_filterButton->height())));
}

void LibraryPage::loadFilters()
{
    const quint64 generation = m_generation;
    m_client->fetchLibraryFilters(
        m_query.parentId, m_query.includeItemTypes, this,
        [this, generation](bool ok, const JellyfinClient::LibraryFilters &filters, const QString &error) {
            if (generation != m_generation)
                return;

            if (!ok) {
                qWarning() << "LibraryPage: fetchLibraryFilters failed:" << error;
                return;
            }

            m_available = filters;
            m_filtersLoaded = true;
        });
}

void LibraryPage::applySort(int comboIndex)
{
    if (comboIndex < 0 || comboIndex >= m_sortCombo->count())
        return;

    m_query.sortBy = m_sortCombo->itemData(comboIndex).toString();
    m_query.ascending = !m_sortDirectionButton->isChecked();
    m_sortDirectionButton->setIcon(
        style()->standardIcon(m_sortDirectionButton->isChecked() ? QStyle::SP_ArrowDown : QStyle::SP_ArrowUp));

    runQuery();
}

void LibraryPage::updateHeader()
{
    m_titleLabel->setText(m_title);
}

void LibraryPage::updateFilterButtonState()
{
    int activeCount = 0;
    if (!m_query.genres.isEmpty())
        ++activeCount;
    if (!m_query.officialRatings.isEmpty())
        ++activeCount;
    if (!m_query.years.isEmpty())
        ++activeCount;
    if (m_query.played != ItemQuery::Played::Any)
        ++activeCount;
    if (m_query.favoritesOnly)
        ++activeCount;

    // Filters are surfaced through both the label text and the button's
    // checked/bold state -- never colour alone (FEATURES.md §C6).
    QFont font = m_filterButton->font();
    font.setBold(activeCount > 0);
    m_filterButton->setFont(font);
    m_filterButton->setChecked(activeCount > 0);
    m_filterButton->setText(activeCount > 0 ? tr("Filter (%1)").arg(activeCount) : tr("Filter"));
}
