#include "DetailPage.h"

#include "api/JellyfinClient.h"
#include "core/Formatting.h"
#include "core/ImageCache.h"
#include "widgets/AsyncImage.h"
#include "widgets/MediaRow.h"
#include "widgets/PosterCard.h"
#include "widgets/SmoothScrollArea.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

constexpr int kPosterWidth = 220;
constexpr int kPosterHeight = 330; // 2:3 poster aspect

// "Breaking Bad · S1:E4 · 2011 · 47m · 8.9 · TV-14" -- episode context leads
// when present, then the usual year/runtime/rating/officialRating chips.
QString buildSubtitle(const MediaItem &item)
{
    QStringList parts;

    if (item.type == QStringLiteral("Episode")) {
        if (!item.seriesName.isEmpty())
            parts << item.seriesName;
        if (item.parentIndexNumber >= 0 && item.indexNumber >= 0)
            parts << QStringLiteral("S%1:E%2").arg(item.parentIndexNumber).arg(item.indexNumber);
    }

    if (item.productionYear > 0)
        parts << QString::number(item.productionYear);
    if (item.runTimeTicks > 0)
        parts << Formatting::formatRuntimeTicks(item.runTimeTicks);
    const QString rating = Formatting::formatRating(item.communityRating);
    if (!rating.isEmpty())
        parts << rating;
    if (!item.officialRating.isEmpty())
        parts << item.officialRating;

    return parts.join(QStringLiteral(" · "));
}

// A whole episode row acts as one big click target. No Q_OBJECT/moc needed --
// plain event-handler overrides plus a std::function are enough, and this
// class never leaves this translation unit.
class ClickableRow : public QWidget
{
public:
    explicit ClickableRow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

    std::function<void()> onActivated;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onActivated)
            onActivated();
        QWidget::mousePressEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && onActivated)
            onActivated();
        else
            QWidget::keyPressEvent(event);
    }
};

} // namespace

DetailPage::DetailPage(JellyfinClient *client, ImageCache *imageCache, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
    , m_imageCache(imageCache)
{
    m_backdrop = new AsyncImage(this);
    m_backdrop->setImageCache(m_imageCache);
    m_backdrop->lower();

    // Scrim sits between the backdrop and the scroll area purely by creation
    // order (later siblings paint on top); resizeEvent() finds it back by
    // object name since it isn't worth a dedicated member for one widget.
    auto *scrim = new QWidget(this);
    scrim->setObjectName(QStringLiteral("backdropScrim"));
    scrim->setAttribute(Qt::WA_TransparentForMouseEvents);
    QPalette scrimPalette = scrim->palette();
    QColor shadow = palette().color(QPalette::Shadow);
    shadow.setAlpha(140);
    scrimPalette.setColor(QPalette::Window, shadow);
    scrim->setPalette(scrimPalette);
    scrim->setAutoFillBackground(true);

    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Let the backdrop + scrim show through everywhere the content column
    // doesn't paint over them.
    m_scrollArea->setAttribute(Qt::WA_TranslucentBackground);
    m_scrollArea->setAutoFillBackground(false);
    m_scrollArea->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    m_scrollArea->viewport()->setAutoFillBackground(false);

    m_content = new QWidget(m_scrollArea);
    m_content->setAutoFillBackground(false);
    m_scrollArea->setWidget(m_content);

    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(32, 24, 32, 32);
    contentLayout->setSpacing(16);

    // --- back button row ---
    m_backButton = new QPushButton(QStringLiteral("Back"), m_content);
    connect(m_backButton, &QPushButton::clicked, this, &DetailPage::backRequested);
    auto *backRow = new QHBoxLayout;
    backRow->addWidget(m_backButton);
    backRow->addStretch(1);
    contentLayout->addLayout(backRow);

    // --- header: poster + metadata ---
    m_poster = new AsyncImage(m_content);
    m_poster->setImageCache(m_imageCache);
    m_poster->setFixedSize(kPosterWidth, kPosterHeight);

    m_titleLabel = new QLabel(m_content);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 12);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);

    m_subtitleLabel = new QLabel(m_content);
    m_subtitleLabel->setWordWrap(true);

    m_genresLabel = new QLabel(m_content);
    m_genresLabel->setWordWrap(true);

    m_taglineLabel = new QLabel(m_content);
    QFont taglineFont = m_taglineLabel->font();
    taglineFont.setItalic(true);
    m_taglineLabel->setFont(taglineFont);
    m_taglineLabel->setWordWrap(true);
    m_taglineLabel->hide();

    m_playButton = new QPushButton(QStringLiteral("Play"), m_content);
    m_playButton->setDefault(true);
    connect(m_playButton, &QPushButton::clicked, this, [this]() { emit playRequested(m_item, false); });

    m_resumeButton = new QPushButton(m_content);
    m_resumeButton->hide();
    connect(m_resumeButton, &QPushButton::clicked, this, [this]() { emit playRequested(m_item, true); });

    m_playedButton = new QPushButton(m_content);
    connect(m_playedButton, &QPushButton::clicked, this, &DetailPage::togglePlayed);

    m_favoriteButton = new QPushButton(m_content);
    connect(m_favoriteButton, &QPushButton::clicked, this, &DetailPage::toggleFavorite);

    auto *actionsRow = new QHBoxLayout;
    actionsRow->addWidget(m_playButton);
    actionsRow->addWidget(m_resumeButton);
    actionsRow->addWidget(m_playedButton);
    actionsRow->addWidget(m_favoriteButton);
    actionsRow->addStretch(1);

    m_overviewLabel = new QLabel(m_content);
    m_overviewLabel->setWordWrap(true);

    auto *metaColumn = new QVBoxLayout;
    metaColumn->addWidget(m_titleLabel);
    metaColumn->addWidget(m_subtitleLabel);
    metaColumn->addWidget(m_genresLabel);
    metaColumn->addWidget(m_taglineLabel);
    metaColumn->addLayout(actionsRow);
    metaColumn->addWidget(m_overviewLabel);

    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(24);
    headerRow->addWidget(m_poster, 0, Qt::AlignTop);
    headerRow->addLayout(metaColumn, 1);
    contentLayout->addLayout(headerRow);

    // --- series-only: season selector + episode list ---
    m_seasonBar = new QWidget(m_content);
    auto *seasonBarLayout = new QHBoxLayout(m_seasonBar);
    seasonBarLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *seasonLabel = new QLabel(QStringLiteral("Season:"), m_seasonBar);
    m_seasonCombo = new QComboBox(m_seasonBar);
    connect(m_seasonCombo, &QComboBox::currentIndexChanged, this, &DetailPage::onSeasonChanged);
    seasonBarLayout->addWidget(seasonLabel);
    seasonBarLayout->addWidget(m_seasonCombo);
    seasonBarLayout->addStretch(1);
    m_seasonBar->hide();
    contentLayout->addWidget(m_seasonBar);

    m_episodeSection = new QWidget(m_content);
    m_episodeLayout = new QVBoxLayout(m_episodeSection);
    m_episodeLayout->setContentsMargins(0, 0, 0, 0);
    m_episodeLayout->setSpacing(4);
    m_episodeSection->hide();
    contentLayout->addWidget(m_episodeSection);

    // --- cast ---
    m_castRow = new MediaRow(QStringLiteral("Cast"), m_content);
    connect(m_castRow, &MediaRow::itemActivated, this, &DetailPage::itemActivated);
    contentLayout->addWidget(m_castRow);

    contentLayout->addStretch(1);
}

void DetailPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    m_backdrop->setGeometry(rect());
    m_backdrop->lower();
    if (QWidget *scrim = findChild<QWidget *>(QStringLiteral("backdropScrim")))
        scrim->setGeometry(rect());
    m_scrollArea->setGeometry(rect());
}

void DetailPage::setItem(const MediaItem &item)
{
    // Bumped before anything else touches m_item: every in-flight callback
    // from a previous item captures the generation it was issued under and
    // compares against m_generation when it lands, so a fast back-and-forward
    // navigation can never paint a stale reply over the item now on screen.
    ++m_generation;
    m_item = item;
    rebuildFromItem();
    m_scrollArea->verticalScrollBar()->setValue(0);

    const quint64 generation = m_generation;
    m_client->fetchItem(item.id, this, [this, generation](bool ok, const MediaItem &fullItem, const QString &) {
        if (generation != m_generation)
            return; // stale: page has since navigated to something else
        if (!ok)
            return;
        m_item = fullItem;
        rebuildFromItem();
    });
}

void DetailPage::rebuildFromItem()
{
    const MediaItem &item = m_item;

    const QString backdropTag = item.backdropTags.isEmpty() ? QString() : item.backdropTags.first();
    m_backdrop->setSource(item.id, ImageCache::ImageType::Backdrop, backdropTag,
                           item.blurHashes.value(QStringLiteral("Backdrop")).value(backdropTag));

    const QString posterTag = item.imageTags.value(QStringLiteral("Primary"));
    m_poster->setSource(item.id, ImageCache::ImageType::Primary, posterTag,
                         item.blurHashes.value(QStringLiteral("Primary")).value(posterTag));

    m_titleLabel->setText(item.name);
    m_subtitleLabel->setText(buildSubtitle(item));

    m_genresLabel->setText(item.genres.join(QStringLiteral(", ")));
    m_genresLabel->setVisible(!item.genres.isEmpty());

    m_taglineLabel->setText(item.tagline);
    m_taglineLabel->setVisible(!item.tagline.isEmpty());

    m_overviewLabel->setText(item.overview);
    m_overviewLabel->setVisible(!item.overview.isEmpty());

    updateActionButtons();

    const bool isSeries = item.type == QStringLiteral("Series");
    m_seasonBar->setVisible(isSeries);
    m_episodeSection->setVisible(isSeries);
    if (isSeries)
        loadSeasons();

    QList<MediaItem> castItems;
    castItems.reserve(item.people.size());
    for (const Person &person : item.people) {
        MediaItem castItem;
        castItem.id = person.id;
        castItem.name = person.name;
        castItem.type = QStringLiteral("Person");
        castItem.imageTags[QStringLiteral("Primary")] = person.primaryImageTag;
        castItems.append(castItem);
    }
    m_castRow->setImageCache(m_imageCache);
    // Empty list makes MediaRow hide itself (MediaRow.h); non-empty shows it.
    m_castRow->setItems(castItems, PosterCard::Shape::Poster);
}

void DetailPage::updateActionButtons()
{
    const bool hasResume = m_item.userData.playbackPositionTicks > 0;
    m_resumeButton->setVisible(hasResume);
    if (hasResume) {
        const qint64 seconds = Formatting::ticksToSeconds(m_item.userData.playbackPositionTicks);
        m_resumeButton->setText(QStringLiteral("Resume from %1").arg(Formatting::formatPosition(seconds)));
    }

    m_playedButton->setText(m_item.userData.played ? QStringLiteral("Watched") : QStringLiteral("Unwatched"));
    m_favoriteButton->setText(m_item.userData.isFavorite ? QStringLiteral("Unfavorite") : QStringLiteral("Favorite"));
}

void DetailPage::loadSeasons()
{
    // Guard against the two rebuildFromItem() calls per setItem() (immediate
    // paint, then the refetch landing) issuing the seasons fetch twice, and
    // against re-fetching when navigating back to a series already loaded.
    if (m_seasonCombo->property("seriesId").toString() == m_item.id)
        return;
    m_seasonCombo->setProperty("seriesId", m_item.id);

    const quint64 generation = m_generation;
    m_client->fetchSeasons(m_item.id, this, [this, generation](bool ok, const QList<MediaItem> &items, const QString &) {
        if (generation != m_generation)
            return;
        if (!ok)
            return;
        m_seasons = items;
        m_seasonCombo->blockSignals(true);
        m_seasonCombo->clear();
        for (const MediaItem &season : items)
            m_seasonCombo->addItem(season.name, season.id);
        m_seasonCombo->blockSignals(false);
        if (!items.isEmpty())
            onSeasonChanged(0);
    });
}

void DetailPage::loadEpisodes(const QString &seasonId)
{
    QLayoutItem *child;
    while ((child = m_episodeLayout->takeAt(0)) != nullptr) {
        if (QWidget *widget = child->widget())
            widget->deleteLater();
        delete child;
    }

    const quint64 generation = m_generation;
    const QString seriesId = m_item.id;
    m_client->fetchEpisodes(seriesId, seasonId, this,
                             [this, generation](bool ok, const QList<MediaItem> &episodes, const QString &) {
                                 if (generation != m_generation)
                                     return;
                                 if (!ok)
                                     return;

                                 for (const MediaItem &episode : episodes) {
                                     auto *row = new ClickableRow(m_episodeSection);
                                     auto *rowLayout = new QHBoxLayout(row);
                                     rowLayout->setContentsMargins(8, 8, 8, 8);

                                     const QString label = episode.indexNumber >= 0
                                         ? QStringLiteral("%1. %2").arg(episode.indexNumber).arg(episode.name)
                                         : episode.name;
                                     auto *nameLabel = new QLabel(label, row);
                                     rowLayout->addWidget(nameLabel, 1);

                                     if (episode.runTimeTicks > 0) {
                                         auto *runtimeLabel =
                                             new QLabel(Formatting::formatRuntimeTicks(episode.runTimeTicks), row);
                                         rowLayout->addWidget(runtimeLabel);
                                     }

                                     if (episode.userData.played) {
                                         auto *playedLabel = new QLabel(QStringLiteral("Watched"), row);
                                         rowLayout->addWidget(playedLabel);
                                     } else if (episode.userData.playedPercentage > 0.0) {
                                         auto *progress = new QProgressBar(row);
                                         progress->setRange(0, 100);
                                         progress->setValue(static_cast<int>(episode.userData.playedPercentage));
                                         progress->setTextVisible(false);
                                         progress->setFixedWidth(60);
                                         rowLayout->addWidget(progress);
                                     }

                                     row->onActivated = [this, episode]() { emit itemActivated(episode); };
                                     m_episodeLayout->addWidget(row);
                                 }
                             });
}

void DetailPage::onSeasonChanged(int index)
{
    if (index < 0 || index >= m_seasonCombo->count())
        return;
    loadEpisodes(m_seasonCombo->itemData(index).toString());
}

void DetailPage::togglePlayed()
{
    // Optimistic: flip locally and repaint now, then roll back if the server
    // rejects it. A toggle that waits for a round trip before responding is
    // the difference between a snappy client and a sluggish one.
    const bool played = !m_item.userData.played;
    const QString itemId = m_item.id;

    m_item.userData.played = played;
    m_item.userData.playedPercentage = played ? 100.0 : 0.0;
    updateActionButtons();

    m_client->setPlayed(itemId, played, this, [this, itemId, played](bool ok, const QString &error) {
        if (!ok) {
            qWarning() << "DetailPage: setPlayed failed:" << error;
            // Only roll back if we're still showing the item we changed --
            // the user may have navigated on while the request was in flight.
            if (m_item.id == itemId) {
                m_item.userData.played = !played;
                m_item.userData.playedPercentage = m_item.userData.played ? 100.0 : 0.0;
                updateActionButtons();
            }
        }
    });
}

void DetailPage::toggleFavorite()
{
    const bool favorite = !m_item.userData.isFavorite;
    const QString itemId = m_item.id;

    m_item.userData.isFavorite = favorite;
    updateActionButtons();

    m_client->setFavorite(itemId, favorite, this, [this, itemId, favorite](bool ok, const QString &error) {
        if (!ok) {
            qWarning() << "DetailPage: setFavorite failed:" << error;
            if (m_item.id == itemId) {
                m_item.userData.isFavorite = !favorite;
                updateActionButtons();
            }
        }
    });
}
