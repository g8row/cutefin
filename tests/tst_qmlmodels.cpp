// Offline unit tests for the QML bridge models (MediaItemModel, HomeSectionModel),
// the static ImageCache bucket/type helpers, and the appearance / dual-core /
// player / recent-search / window-geometry Settings properties.
//
// Settings runs against a QTemporaryDir ini file, so nothing touches the real
// user preferences. No network, no GUI, no libmpv.

#include "api/Models.h"
#include "app/HomeSectionModel.h"
#include "app/MediaItemModel.h"
#include "core/Formatting.h"
#include "core/ImageCache.h"
#include "core/Settings.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <memory>
#include <optional>

namespace {

MediaItem movie(const QString &id, const QString &name)
{
    MediaItem item;
    item.id = id;
    item.name = name;
    item.type = QStringLiteral("Movie");
    item.productionYear = 2010;
    item.runTimeTicks = Formatting::secondsToTicks(112 * 60);
    return item;
}

MediaItem episode(const QString &id, int season, int number)
{
    MediaItem item;
    item.id = id;
    item.name = QStringLiteral("Episode %1").arg(number);
    item.type = QStringLiteral("Episode");
    item.seriesId = QStringLiteral("series-1");
    item.seriesName = QStringLiteral("Severance");
    item.parentIndexNumber = season;
    item.indexNumber = number;
    return item;
}

QList<MediaItem> moviesWithIds(const QStringList &ids)
{
    QList<MediaItem> items;
    for (const QString &id : ids)
        items.append(movie(id, QStringLiteral("Title %1").arg(id)));
    return items;
}

MediaItemModel *modelAt(const HomeSectionModel &sections, int row)
{
    return sections.data(sections.index(row), HomeSectionModel::ItemsModelRole).value<MediaItemModel *>();
}

QStringList keysOf(const HomeSectionModel &sections)
{
    QStringList keys;
    for (int row = 0; row < sections.rowCount(); ++row)
        keys.append(sections.data(sections.index(row), HomeSectionModel::KeyRole).toString());
    return keys;
}

void addSection(HomeSectionModel &sections, const QString &key, int sortOrder, const QStringList &ids)
{
    sections.upsertSection(key, QStringLiteral("Title of %1").arg(key), QStringLiteral("latest"), false,
                           QStringLiteral("lib-1"), sortOrder, moviesWithIds(ids));
}

} // namespace

class tst_QmlModels : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // --- MediaItemModel ---
    void setItemsResetsOnNewIdsAndEmitsCount();
    void setItemsSameIdsUpdatesInPlace();
    void setItemsEmptyOnEmptyModelIsNoop();
    void appendItemsInsertsRowsAndEmitsCount();
    void clearEmptiesModelOnce();
    void roleNamesCoverEveryRoleWithUniqueNames();
    void displayRoleMapsToName();
    void invalidIndexReturnsInvalidVariant();
    void movieDataRoles();
    void episodeDataRoles();
    void episodeCodeAndSubtitleFormatting();
    void backdropFallsBackToParentItem();
    void resumeAndRemainingText();
    void playedPercentageIsComputedWhenNotStored();
    void isPlayableByType_data();
    void isPlayableByType();
    void getReturnsVariantMapOrEmpty();
    void itemByIdFindsOrMisses();
    void updateUserStateMirrorsAndSignalsOnlyChanges();

    // --- HomeSectionModel ---
    void upsertSortsBySortOrderStably();
    void upsertSameKeyReplacesInPlace();
    void upsertSameKeyNewSortOrderMovesSectionKeepingModel();
    void upsertEmptyItemsRemovesSection();
    void removeSectionDeletesChildModel();
    void clearRemovesAllSections();
    void sectionRolesExposeFields();
    void updateUserStatePropagatesToEverySection();
    void findItemByIdSearchesAllSections();

    // --- ImageCache static helpers ---
    void bucketWidthRoundsAndClamps_data();
    void bucketWidthRoundsAndClamps();
    void imageTypeToStringValues();
    void imageTypeStringRoundTrip();
    void imageTypeFromStringIsCaseInsensitiveAndRejectsUnknown();

    // --- Settings ---
    void appearanceDefaultsAndRoundTripAcrossInstances();
    void dualCoreDefaultFollowsCpuCount();
    void dualCoreExplicitValueWins();
    void videoProfileFollowsDualCoreUnlessSet();
    void hwdecModeDefaultAndRoundTrip();
    void maxBitrateDefaultsToAutoAndClampsNegative();
    void customMpvOptionsAndLogLevelDefaultsAndRoundTrip();
    void recentSearchesTrimDedupeAndMoveToFront();
    void recentSearchesAreCappedAtTen();
    void clearRecentSearchesRemovesPersistedList();
    void windowStateDefaultsAndRoundTrip();
    void windowStateRejectsTooSmallSizes();
    void windowStateReadClampsToBounds();

private:
    QString iniPath() const { return m_dir->filePath(QStringLiteral("settings.ini")); }

    std::unique_ptr<QTemporaryDir> m_dir;
};

void tst_QmlModels::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void tst_QmlModels::cleanup()
{
    m_dir.reset();
}

// ===========================================================================
// MediaItemModel
// ===========================================================================

void tst_QmlModels::setItemsResetsOnNewIdsAndEmitsCount()
{
    MediaItemModel model;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy countSpy(&model, &MediaItemModel::countChanged);

    model.setItems(moviesWithIds({QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(model.data(model.index(1), MediaItemModel::NameRole).toString(), QStringLiteral("Title b"));

    model.setItems(moviesWithIds({QStringLiteral("c")}));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(resetSpy.count(), 2);
    QCOMPARE(countSpy.count(), 2);
}

void tst_QmlModels::setItemsSameIdsUpdatesInPlace()
{
    MediaItemModel model;
    model.setItems(moviesWithIds({QStringLiteral("a"), QStringLiteral("b")}));

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy countSpy(&model, &MediaItemModel::countChanged);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

    QList<MediaItem> refreshed = moviesWithIds({QStringLiteral("a"), QStringLiteral("b")});
    refreshed[0].name = QStringLiteral("Renamed");
    refreshed[0].userData.isFavorite = true;
    model.setItems(refreshed);

    // Same ids in the same order: no reset, so views keep their delegates.
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(countSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(model.data(model.index(0), MediaItemModel::NameRole).toString(), QStringLiteral("Renamed"));
    QVERIFY(model.data(model.index(0), MediaItemModel::IsFavoriteRole).toBool());
}

void tst_QmlModels::setItemsEmptyOnEmptyModelIsNoop()
{
    MediaItemModel model;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy countSpy(&model, &MediaItemModel::countChanged);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

    model.setItems(QList<MediaItem>());
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(countSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);
}

void tst_QmlModels::appendItemsInsertsRowsAndEmitsCount()
{
    MediaItemModel model;
    model.setItems(moviesWithIds({QStringLiteral("a")}));

    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy countSpy(&model, &MediaItemModel::countChanged);

    model.appendItems(moviesWithIds({QStringLiteral("b"), QStringLiteral("c")}));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(insertedSpy.at(0).at(1).toInt(), 1);
    QCOMPARE(insertedSpy.at(0).at(2).toInt(), 2);
    QCOMPARE(countSpy.count(), 1);

    model.appendItems(QList<MediaItem>());
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);
}

void tst_QmlModels::clearEmptiesModelOnce()
{
    MediaItemModel model;
    model.setItems(moviesWithIds({QStringLiteral("a"), QStringLiteral("b")}));

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy countSpy(&model, &MediaItemModel::countChanged);

    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);

    // Clearing an already-empty model must not announce anything.
    model.clear();
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);
}

void tst_QmlModels::roleNamesCoverEveryRoleWithUniqueNames()
{
    MediaItemModel model;
    const QHash<int, QByteArray> names = model.roleNames();

    QSet<QByteArray> unique;
    for (int role = MediaItemModel::IdRole; role <= MediaItemModel::IsPlayableRole; ++role) {
        QVERIFY(names.contains(role));
        unique.insert(names.value(role));
    }
    // Every entry is one of the enum roles, and no two roles share a QML name.
    QCOMPARE(unique.size(), names.size());

    QCOMPARE(names.value(MediaItemModel::IdRole), QByteArrayLiteral("id"));
    QCOMPARE(names.value(MediaItemModel::EpisodeCodeRole), QByteArrayLiteral("episodeCode"));
    QCOMPARE(names.value(MediaItemModel::IsPlayableRole), QByteArrayLiteral("isPlayable"));
}

void tst_QmlModels::displayRoleMapsToName()
{
    MediaItemModel model;
    model.setItems(moviesWithIds({QStringLiteral("a")}));
    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("Title a"));
}

void tst_QmlModels::invalidIndexReturnsInvalidVariant()
{
    MediaItemModel model;
    model.setItems(moviesWithIds({QStringLiteral("a")}));

    QVERIFY(!model.data(QModelIndex(), MediaItemModel::NameRole).isValid());
    QVERIFY(!model.data(model.index(5), MediaItemModel::NameRole).isValid());
}

void tst_QmlModels::movieDataRoles()
{
    MediaItem item = movie(QStringLiteral("m1"), QStringLiteral("Dune"));
    item.runTimeTicks = Formatting::secondsToTicks(155 * 60);
    item.communityRating = 6.9;
    item.officialRating = QStringLiteral("PG-13");
    item.imageTags.insert(QStringLiteral("Primary"), QStringLiteral("p-tag"));

    MediaItemModel model;
    model.setItems({item});
    const QModelIndex idx = model.index(0);

    QCOMPARE(model.data(idx, MediaItemModel::IdRole).toString(), QStringLiteral("m1"));
    QCOMPARE(model.data(idx, MediaItemModel::NameRole).toString(), QStringLiteral("Dune"));
    QCOMPARE(model.data(idx, MediaItemModel::TypeRole).toString(), QStringLiteral("Movie"));
    QCOMPARE(model.data(idx, MediaItemModel::YearRole).toInt(), 2010);
    QCOMPARE(model.data(idx, MediaItemModel::SubtitleTextRole).toString(), QStringLiteral("2010"));
    QCOMPARE(model.data(idx, MediaItemModel::RuntimeTextRole).toString(), QStringLiteral("2h 35m"));
    QCOMPARE(model.data(idx, MediaItemModel::RatingTextRole).toString(), QStringLiteral("6.9"));
    QCOMPARE(model.data(idx, MediaItemModel::OfficialRatingRole).toString(), QStringLiteral("PG-13"));
    QCOMPARE(model.data(idx, MediaItemModel::PrimaryTagRole).toString(), QStringLiteral("p-tag"));
    QCOMPARE(model.data(idx, MediaItemModel::EpisodeCodeRole).toString(), QString());
    QVERIFY(model.data(idx, MediaItemModel::IsPlayableRole).toBool());
    QVERIFY(!model.data(idx, MediaItemModel::PlayedRole).toBool());
    QVERIFY(!model.data(idx, MediaItemModel::IsFavoriteRole).toBool());
    QCOMPARE(model.data(idx, MediaItemModel::ResumePositionTextRole).toString(), QString());
    QCOMPARE(model.data(idx, MediaItemModel::RemainingTextRole).toString(), QString());
}

void tst_QmlModels::episodeDataRoles()
{
    MediaItemModel model;
    model.setItems({episode(QStringLiteral("e1"), 1, 4)});
    const QModelIndex idx = model.index(0);

    QCOMPARE(model.data(idx, MediaItemModel::TypeRole).toString(), QStringLiteral("Episode"));
    QCOMPARE(model.data(idx, MediaItemModel::SeriesIdRole).toString(), QStringLiteral("series-1"));
    QCOMPARE(model.data(idx, MediaItemModel::SeriesNameRole).toString(), QStringLiteral("Severance"));
    QCOMPARE(model.data(idx, MediaItemModel::EpisodeCodeRole).toString(), QStringLiteral("S01E04"));
    QCOMPARE(model.data(idx, MediaItemModel::SubtitleTextRole).toString(),
             QStringLiteral("Severance · S01E04"));
    QVERIFY(model.data(idx, MediaItemModel::IsPlayableRole).toBool());
}

void tst_QmlModels::episodeCodeAndSubtitleFormatting()
{
    QCOMPARE(MediaItemModel::episodeCodeFor(episode(QStringLiteral("e"), 1, 4)), QStringLiteral("S01E04"));
    QCOMPARE(MediaItemModel::episodeCodeFor(episode(QStringLiteral("e"), 10, 12)), QStringLiteral("S10E12"));

    // Missing season number: the episode part alone.
    MediaItem noSeason = episode(QStringLiteral("e"), -1, 3);
    QCOMPARE(MediaItemModel::episodeCodeFor(noSeason), QStringLiteral("E03"));

    // Missing episode number: no code at all.
    MediaItem noEpisode = episode(QStringLiteral("e"), 1, -1);
    QCOMPARE(MediaItemModel::episodeCodeFor(noEpisode), QString());

    // Only episodes carry a code.
    MediaItem notEpisode = movie(QStringLiteral("m"), QStringLiteral("Movie"));
    notEpisode.indexNumber = 2;
    notEpisode.parentIndexNumber = 1;
    QCOMPARE(MediaItemModel::episodeCodeFor(notEpisode), QString());

    // Episode subtitle without a series name is just the code.
    MediaItem orphan = episode(QStringLiteral("e"), 2, 7);
    orphan.seriesName.clear();
    QCOMPARE(MediaItemModel::subtitleFor(orphan), QStringLiteral("S02E07"));

    MediaItem season = movie(QStringLiteral("s"), QStringLiteral("Season 2"));
    season.type = QStringLiteral("Season");
    season.seriesName = QStringLiteral("Severance");
    QCOMPARE(MediaItemModel::subtitleFor(season), QStringLiteral("Severance"));

    MediaItem undated = movie(QStringLiteral("u"), QStringLiteral("Undated"));
    undated.productionYear = 0;
    QCOMPARE(MediaItemModel::subtitleFor(undated), QString());
}

void tst_QmlModels::backdropFallsBackToParentItem()
{
    // Episode with no backdrop of its own: borrows the series backdrop.
    MediaItem inherited = episode(QStringLiteral("e1"), 1, 1);
    inherited.parentBackdropTags = QStringList(QStringLiteral("pb1"));
    inherited.parentBackdropItemId = QStringLiteral("series-42");
    inherited.blurHashes[QStringLiteral("Backdrop")].insert(QStringLiteral("pb1"), QStringLiteral("bh-pb1"));

    // No explicit parent item id: falls back to the series id.
    MediaItem viaSeries = inherited;
    viaSeries.id = QStringLiteral("e2");
    viaSeries.parentBackdropItemId.clear();

    // No parent id and no series id: falls back to the item itself.
    MediaItem selfOnly = inherited;
    selfOnly.id = QStringLiteral("e3");
    selfOnly.parentBackdropItemId.clear();
    selfOnly.seriesId.clear();

    // An item's own backdrop always wins over the parent's.
    MediaItem own = inherited;
    own.id = QStringLiteral("e4");
    own.backdropTags = QStringList(QStringLiteral("own"));

    // Nothing anywhere: empty tag, own id, empty blurhash.
    MediaItem bare = episode(QStringLiteral("e5"), 1, 5);
    bare.seriesId.clear();

    MediaItemModel model;
    model.setItems({inherited, viaSeries, selfOnly, own, bare});

    QCOMPARE(model.data(model.index(0), MediaItemModel::BackdropTagRole).toString(), QStringLiteral("pb1"));
    QCOMPARE(model.data(model.index(0), MediaItemModel::BackdropItemIdRole).toString(),
             QStringLiteral("series-42"));
    QCOMPARE(model.data(model.index(0), MediaItemModel::BackdropBlurHashRole).toString(),
             QStringLiteral("bh-pb1"));

    QCOMPARE(model.data(model.index(1), MediaItemModel::BackdropItemIdRole).toString(),
             QStringLiteral("series-1"));

    QCOMPARE(model.data(model.index(2), MediaItemModel::BackdropItemIdRole).toString(),
             QStringLiteral("e3"));

    QCOMPARE(model.data(model.index(3), MediaItemModel::BackdropTagRole).toString(), QStringLiteral("own"));
    QCOMPARE(model.data(model.index(3), MediaItemModel::BackdropItemIdRole).toString(), QStringLiteral("e4"));

    QCOMPARE(model.data(model.index(4), MediaItemModel::BackdropTagRole).toString(), QString());
    QCOMPARE(model.data(model.index(4), MediaItemModel::BackdropItemIdRole).toString(), QStringLiteral("e5"));
    QCOMPARE(model.data(model.index(4), MediaItemModel::BackdropBlurHashRole).toString(), QString());
}

void tst_QmlModels::resumeAndRemainingText()
{
    MediaItem partial = movie(QStringLiteral("p"), QStringLiteral("Partial"));
    partial.runTimeTicks = Formatting::secondsToTicks(3600);
    partial.userData.playbackPositionTicks = Formatting::secondsToTicks(1200);

    MediaItem longResume = movie(QStringLiteral("l"), QStringLiteral("Long"));
    longResume.runTimeTicks = Formatting::secondsToTicks(4 * 3600);
    longResume.userData.playbackPositionTicks = Formatting::secondsToTicks(3725);

    // Position past the end (bad server data): no "left" text, clamped percentage.
    MediaItem overrun = movie(QStringLiteral("o"), QStringLiteral("Overrun"));
    overrun.runTimeTicks = Formatting::secondsToTicks(3600);
    overrun.userData.playbackPositionTicks = Formatting::secondsToTicks(4000);

    MediaItem fresh = movie(QStringLiteral("f"), QStringLiteral("Fresh"));

    MediaItemModel model;
    model.setItems({partial, longResume, overrun, fresh});

    QCOMPARE(model.data(model.index(0), MediaItemModel::ResumePositionTextRole).toString(),
             QStringLiteral("20:00"));
    QCOMPARE(model.data(model.index(0), MediaItemModel::RemainingTextRole).toString(),
             QStringLiteral("40m left"));
    QCOMPARE(model.data(model.index(0), MediaItemModel::PlaybackPositionTicksRole).toLongLong(),
             Formatting::secondsToTicks(1200));

    QCOMPARE(model.data(model.index(1), MediaItemModel::ResumePositionTextRole).toString(),
             QStringLiteral("1:02:05"));
    QCOMPARE(model.data(model.index(1), MediaItemModel::RemainingTextRole).toString(),
             QStringLiteral("2h 57m left"));

    QCOMPARE(model.data(model.index(2), MediaItemModel::RemainingTextRole).toString(), QString());
    QCOMPARE(model.data(model.index(2), MediaItemModel::PlayedPercentageRole).toDouble(), 100.0);

    QCOMPARE(model.data(model.index(3), MediaItemModel::ResumePositionTextRole).toString(), QString());
    QCOMPARE(model.data(model.index(3), MediaItemModel::RemainingTextRole).toString(), QString());
}

void tst_QmlModels::playedPercentageIsComputedWhenNotStored()
{
    MediaItem computed = movie(QStringLiteral("c"), QStringLiteral("Computed"));
    computed.runTimeTicks = Formatting::secondsToTicks(3600);
    computed.userData.playbackPositionTicks = Formatting::secondsToTicks(1200);

    // A stored percentage takes precedence over the computed one.
    MediaItem stored = computed;
    stored.userData.playedPercentage = 75.0;

    MediaItemModel model;
    model.setItems({computed, stored});

    const double computedPct = model.data(model.index(0), MediaItemModel::PlayedPercentageRole).toDouble();
    QVERIFY(qAbs(computedPct - (100.0 * 1200.0 / 3600.0)) < 0.001);
    QCOMPARE(model.data(model.index(1), MediaItemModel::PlayedPercentageRole).toDouble(), 75.0);
}

void tst_QmlModels::isPlayableByType_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<bool>("playable");

    QTest::newRow("Movie") << QStringLiteral("Movie") << true;
    QTest::newRow("Episode") << QStringLiteral("Episode") << true;
    QTest::newRow("Video") << QStringLiteral("Video") << true;
    QTest::newRow("MusicVideo") << QStringLiteral("MusicVideo") << true;
    QTest::newRow("Series") << QStringLiteral("Series") << false;
    QTest::newRow("Season") << QStringLiteral("Season") << false;
    QTest::newRow("CollectionFolder") << QStringLiteral("CollectionFolder") << false;
    QTest::newRow("empty") << QString() << false;
}

void tst_QmlModels::isPlayableByType()
{
    QFETCH(QString, type);
    QFETCH(bool, playable);

    MediaItem item = movie(QStringLiteral("x"), QStringLiteral("X"));
    item.type = type;

    MediaItemModel model;
    model.setItems({item});
    QCOMPARE(model.data(model.index(0), MediaItemModel::IsPlayableRole).toBool(), playable);
}

void tst_QmlModels::getReturnsVariantMapOrEmpty()
{
    MediaItemModel model;
    model.setItems({episode(QStringLiteral("e1"), 1, 4)});

    const QVariantMap row = model.get(0);
    QCOMPARE(row.size(), model.roleNames().size());
    QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("Episode 4"));
    QCOMPARE(row.value(QStringLiteral("episodeCode")).toString(), QStringLiteral("S01E04"));
    QVERIFY(row.value(QStringLiteral("isPlayable")).toBool());

    QVERIFY(model.get(-1).isEmpty());
    QVERIFY(model.get(1).isEmpty());
}

void tst_QmlModels::itemByIdFindsOrMisses()
{
    MediaItemModel model;
    model.setItems(moviesWithIds({QStringLiteral("a"), QStringLiteral("b")}));

    const std::optional<MediaItem> found = model.itemById(QStringLiteral("b"));
    QVERIFY(found.has_value());
    QCOMPARE(found->name, QStringLiteral("Title b"));

    QVERIFY(!model.itemById(QStringLiteral("zzz")).has_value());
}

void tst_QmlModels::updateUserStateMirrorsAndSignalsOnlyChanges()
{
    MediaItem partial = movie(QStringLiteral("a"), QStringLiteral("Alpha"));
    partial.runTimeTicks = Formatting::secondsToTicks(3600);
    partial.userData.playbackPositionTicks = Formatting::secondsToTicks(600);

    MediaItemModel model;
    model.setItems({partial, movie(QStringLiteral("b"), QStringLiteral("Beta"))});
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

    // Favouriting alone keeps the resume point.
    model.updateUserState(QStringLiteral("a"), false, true);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(model.data(model.index(0), MediaItemModel::IsFavoriteRole).toBool());
    QCOMPARE(model.data(model.index(0), MediaItemModel::PlaybackPositionTicksRole).toLongLong(),
             Formatting::secondsToTicks(600));
    const QList<int> roles = changedSpy.at(0).at(2).value<QList<int>>();
    QVERIFY(roles.contains(MediaItemModel::IsFavoriteRole));
    QVERIFY(roles.contains(MediaItemModel::PlayedRole));

    // Marking played drops the resume point straight away.
    model.updateUserState(QStringLiteral("a"), true, true);
    QCOMPARE(changedSpy.count(), 2);
    QVERIFY(model.data(model.index(0), MediaItemModel::PlayedRole).toBool());
    QCOMPARE(model.data(model.index(0), MediaItemModel::PlaybackPositionTicksRole).toLongLong(), 0LL);
    QCOMPARE(model.data(model.index(0), MediaItemModel::ResumePositionTextRole).toString(), QString());
    QCOMPARE(model.data(model.index(0), MediaItemModel::RemainingTextRole).toString(), QString());
    QCOMPARE(model.data(model.index(0), MediaItemModel::PlayedPercentageRole).toDouble(), 0.0);

    // Re-applying the same state, or an unknown id, emits nothing.
    model.updateUserState(QStringLiteral("a"), true, true);
    model.updateUserState(QStringLiteral("missing"), true, true);
    QCOMPARE(changedSpy.count(), 2);

    // Other rows are untouched.
    QVERIFY(!model.data(model.index(1), MediaItemModel::PlayedRole).toBool());
    QVERIFY(!model.data(model.index(1), MediaItemModel::IsFavoriteRole).toBool());
}

// ===========================================================================
// HomeSectionModel
// ===========================================================================

void tst_QmlModels::upsertSortsBySortOrderStably()
{
    HomeSectionModel sections;
    addSection(sections, QStringLiteral("b"), 20, {QStringLiteral("b1")});
    addSection(sections, QStringLiteral("a"), 10, {QStringLiteral("a1")});
    addSection(sections, QStringLiteral("c"), 30, {QStringLiteral("c1")});
    // Ties keep insertion order: "d" arrives after "b" with the same sortOrder.
    addSection(sections, QStringLiteral("d"), 20, {QStringLiteral("d1")});

    QCOMPARE(sections.rowCount(), 4);
    QCOMPARE(keysOf(sections), (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("d"),
                                            QStringLiteral("c")}));
}

void tst_QmlModels::upsertSameKeyReplacesInPlace()
{
    HomeSectionModel sections;
    sections.upsertSection(QStringLiteral("latest"), QStringLiteral("Latest Movies"), QStringLiteral("latest"),
                           false, QStringLiteral("lib-1"), 20,
                           moviesWithIds({QStringLiteral("m1"), QStringLiteral("m2")}));
    MediaItemModel *original = modelAt(sections, 0);
    QVERIFY(original);

    QSignalSpy countSpy(&sections, &HomeSectionModel::countChanged);

    sections.upsertSection(QStringLiteral("latest"), QStringLiteral("Newest"), QStringLiteral("latest"), false,
                           QStringLiteral("lib-1"), 20, moviesWithIds({QStringLiteral("m3")}));

    QCOMPARE(sections.rowCount(), 1);
    QCOMPARE(countSpy.count(), 0);
    QCOMPARE(sections.data(sections.index(0), HomeSectionModel::TitleRole).toString(), QStringLiteral("Newest"));

    // The child model is reused, and its contents are replaced.
    QCOMPARE(modelAt(sections, 0), original);
    QCOMPARE(original->rowCount(), 1);
    QVERIFY(!original->itemById(QStringLiteral("m1")).has_value());
    QVERIFY(original->itemById(QStringLiteral("m3")).has_value());
}

void tst_QmlModels::upsertSameKeyNewSortOrderMovesSectionKeepingModel()
{
    HomeSectionModel sections;
    addSection(sections, QStringLiteral("a"), 10, {QStringLiteral("a1")});
    addSection(sections, QStringLiteral("b"), 20, {QStringLiteral("b1")});
    addSection(sections, QStringLiteral("c"), 30, {QStringLiteral("c1")});

    MediaItemModel *aModel = modelAt(sections, 0);
    QVERIFY(aModel);

    addSection(sections, QStringLiteral("a"), 25, {QStringLiteral("a2")});

    QCOMPARE(sections.rowCount(), 3);
    QCOMPARE(keysOf(sections), (QStringList{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")}));
    QCOMPARE(modelAt(sections, 1), aModel);
    QVERIFY(aModel->itemById(QStringLiteral("a2")).has_value());
    QVERIFY(!aModel->itemById(QStringLiteral("a1")).has_value());
}

void tst_QmlModels::upsertEmptyItemsRemovesSection()
{
    HomeSectionModel sections;
    addSection(sections, QStringLiteral("a"), 10, {QStringLiteral("a1")});
    addSection(sections, QStringLiteral("b"), 20, {QStringLiteral("b1")});

    QSignalSpy countSpy(&sections, &HomeSectionModel::countChanged);

    addSection(sections, QStringLiteral("a"), 10, {});
    QCOMPARE(sections.rowCount(), 1);
    QCOMPARE(keysOf(sections), QStringList{QStringLiteral("b")});
    QCOMPARE(countSpy.count(), 1);

    // Emptying a section that is not there is a no-op.
    addSection(sections, QStringLiteral("ghost"), 40, {});
    QCOMPARE(sections.rowCount(), 1);
    QCOMPARE(countSpy.count(), 1);
}

void tst_QmlModels::removeSectionDeletesChildModel()
{
    HomeSectionModel sections;
    addSection(sections, QStringLiteral("a"), 10, {QStringLiteral("a1")});

    MediaItemModel *child = modelAt(sections, 0);
    QVERIFY(child);
    QPointer<MediaItemModel> guard(child);

    sections.removeSection(QStringLiteral("a"));
    QCOMPARE(sections.rowCount(), 0);

    // The child is deleted later so delegates still tearing down never see a dead model.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(guard.isNull());
}

void tst_QmlModels::clearRemovesAllSections()
{
    HomeSectionModel sections;
    addSection(sections, QStringLiteral("a"), 10, {QStringLiteral("a1")});
    addSection(sections, QStringLiteral("b"), 20, {QStringLiteral("b1")});

    QSignalSpy countSpy(&sections, &HomeSectionModel::countChanged);

    sections.clear();
    QCOMPARE(sections.rowCount(), 0);
    QCOMPARE(countSpy.count(), 1);

    sections.clear();
    QCOMPARE(countSpy.count(), 1);
}

void tst_QmlModels::sectionRolesExposeFields()
{
    HomeSectionModel sections;
    sections.upsertSection(QStringLiteral("resume"), QStringLiteral("Continue Watching"),
                           QStringLiteral("resume"), true, QStringLiteral("lib-1"), 10,
                           moviesWithIds({QStringLiteral("x")}));

    const QModelIndex idx = sections.index(0);
    QCOMPARE(sections.data(idx, HomeSectionModel::KeyRole).toString(), QStringLiteral("resume"));
    QCOMPARE(sections.data(idx, HomeSectionModel::TitleRole).toString(), QStringLiteral("Continue Watching"));
    QCOMPARE(sections.data(idx, Qt::DisplayRole).toString(), QStringLiteral("Continue Watching"));
    QCOMPARE(sections.data(idx, HomeSectionModel::KindRole).toString(), QStringLiteral("resume"));
    QVERIFY(sections.data(idx, HomeSectionModel::LandscapeRole).toBool());
    QCOMPARE(sections.data(idx, HomeSectionModel::LibraryIdRole).toString(), QStringLiteral("lib-1"));

    MediaItemModel *child = modelAt(sections, 0);
    QVERIFY(child);
    QCOMPARE(child->rowCount(), 1);

    QVERIFY(!sections.data(idx, 999).isValid());
    QVERIFY(!sections.data(QModelIndex(), HomeSectionModel::KeyRole).isValid());

    const QHash<int, QByteArray> names = sections.roleNames();
    QCOMPARE(names.value(HomeSectionModel::ItemsModelRole), QByteArrayLiteral("itemsModel"));
    QCOMPARE(names.value(HomeSectionModel::LandscapeRole), QByteArrayLiteral("landscape"));
}

void tst_QmlModels::updateUserStatePropagatesToEverySection()
{
    HomeSectionModel sections;
    addSection(sections, QStringLiteral("a"), 10, {QStringLiteral("shared"), QStringLiteral("only-a")});
    addSection(sections, QStringLiteral("b"), 20, {QStringLiteral("shared")});

    sections.updateUserState(QStringLiteral("shared"), true, true);

    // Every live section sees the change, not just the first one holding the item.
    for (int row = 0; row < sections.rowCount(); ++row) {
        MediaItemModel *child = modelAt(sections, row);
        QVERIFY(child);
        const std::optional<MediaItem> item = child->itemById(QStringLiteral("shared"));
        QVERIFY(item.has_value());
        QVERIFY(item->userData.played);
        QVERIFY(item->userData.isFavorite);
    }

    const std::optional<MediaItem> onlyA = modelAt(sections, 0)->itemById(QStringLiteral("only-a"));
    QVERIFY(onlyA.has_value());
    QVERIFY(!onlyA->userData.played);
    QVERIFY(!onlyA->userData.isFavorite);
}

void tst_QmlModels::findItemByIdSearchesAllSections()
{
    HomeSectionModel sections;
    addSection(sections, QStringLiteral("a"), 10, {QStringLiteral("x")});
    addSection(sections, QStringLiteral("b"), 20, {QStringLiteral("y")});

    const std::optional<MediaItem> found = sections.findItemById(QStringLiteral("y"));
    QVERIFY(found.has_value());
    QCOMPARE(found->name, QStringLiteral("Title y"));

    QVERIFY(!sections.findItemById(QStringLiteral("missing")).has_value());
}

// ===========================================================================
// ImageCache static helpers
// ===========================================================================

void tst_QmlModels::bucketWidthRoundsAndClamps_data()
{
    QTest::addColumn<int>("requested");
    QTest::addColumn<int>("expected");

    QTest::newRow("zero falls back to default") << 0 << 300;
    QTest::newRow("negative falls back to default") << -50 << 300;
    QTest::newRow("tiny clamps to floor") << 1 << 200;
    QTest::newRow("150 rounds to floor") << 150 << 200;
    QTest::newRow("exact floor") << 200 << 200;
    QTest::newRow("201 rounds up") << 201 << 300;
    QTest::newRow("250 rounds up") << 250 << 300;
    QTest::newRow("999 rounds up") << 999 << 1000;
    QTest::newRow("exact step") << 1000 << 1000;
    QTest::newRow("1001 rounds up") << 1001 << 1100;
    QTest::newRow("exact ceiling") << 1920 << 1920;
    QTest::newRow("1921 clamps to ceiling") << 1921 << 1920;
    QTest::newRow("4K clamps to ceiling") << 3840 << 1920;
}

void tst_QmlModels::bucketWidthRoundsAndClamps()
{
    QFETCH(int, requested);
    QFETCH(int, expected);
    QCOMPARE(ImageCache::bucketWidth(requested), expected);
}

void tst_QmlModels::imageTypeToStringValues()
{
    QCOMPARE(ImageCache::imageTypeToString(ImageCache::ImageType::Primary), QStringLiteral("Primary"));
    QCOMPARE(ImageCache::imageTypeToString(ImageCache::ImageType::Backdrop), QStringLiteral("Backdrop"));
    QCOMPARE(ImageCache::imageTypeToString(ImageCache::ImageType::Thumb), QStringLiteral("Thumb"));
    QCOMPARE(ImageCache::imageTypeToString(ImageCache::ImageType::Logo), QStringLiteral("Logo"));
}

void tst_QmlModels::imageTypeStringRoundTrip()
{
    const ImageCache::ImageType types[] = {
        ImageCache::ImageType::Primary,
        ImageCache::ImageType::Backdrop,
        ImageCache::ImageType::Thumb,
        ImageCache::ImageType::Logo,
    };
    for (const ImageCache::ImageType type : types) {
        bool ok = false;
        const ImageCache::ImageType parsed =
            ImageCache::imageTypeFromString(ImageCache::imageTypeToString(type), &ok);
        QVERIFY(ok);
        QCOMPARE(static_cast<int>(parsed), static_cast<int>(type));
    }
}

void tst_QmlModels::imageTypeFromStringIsCaseInsensitiveAndRejectsUnknown()
{
    bool ok = false;

    const ImageCache::ImageType lower = ImageCache::imageTypeFromString(QStringLiteral("backdrop"), &ok);
    QVERIFY(ok);
    QCOMPARE(static_cast<int>(lower), static_cast<int>(ImageCache::ImageType::Backdrop));

    const ImageCache::ImageType upper = ImageCache::imageTypeFromString(QStringLiteral("LOGO"), &ok);
    QVERIFY(ok);
    QCOMPARE(static_cast<int>(upper), static_cast<int>(ImageCache::ImageType::Logo));

    const ImageCache::ImageType unknown = ImageCache::imageTypeFromString(QStringLiteral("Banner"), &ok);
    QVERIFY(!ok);
    QCOMPARE(static_cast<int>(unknown), static_cast<int>(ImageCache::ImageType::Primary));

    const ImageCache::ImageType empty = ImageCache::imageTypeFromString(QString(), &ok);
    QVERIFY(!ok);
    QCOMPARE(static_cast<int>(empty), static_cast<int>(ImageCache::ImageType::Primary));

    // The out-parameter is optional.
    const ImageCache::ImageType noFlag = ImageCache::imageTypeFromString(QStringLiteral("Thumb"));
    QCOMPARE(static_cast<int>(noFlag), static_cast<int>(ImageCache::ImageType::Thumb));
}

// ===========================================================================
// Settings: appearance, dual-core, player, recent searches, window geometry
// ===========================================================================

void tst_QmlModels::appearanceDefaultsAndRoundTripAcrossInstances()
{
    {
        Settings defaults(iniPath());
        QCOMPARE(defaults.themeMode(), QStringLiteral("dark"));
        QCOMPARE(defaults.accentColor(), QStringLiteral("#AA5CC3"));
        QVERIFY(!defaults.reduceMotion());
    }

    {
        Settings writer(iniPath());
        writer.setThemeMode(QStringLiteral("light"));
        writer.setAccentColor(QStringLiteral("#FF2E86DE"));
        writer.setReduceMotion(true);
    }

    Settings reader(iniPath());
    QCOMPARE(reader.themeMode(), QStringLiteral("light"));
    QCOMPARE(reader.accentColor(), QStringLiteral("#FF2E86DE"));
    QVERIFY(reader.reduceMotion());
}

void tst_QmlModels::dualCoreDefaultFollowsCpuCount()
{
    Settings s(iniPath());
    // Unset: auto-enabled on machines with at most two logical cores.
    QCOMPARE(s.dualCoreMode(), QThread::idealThreadCount() <= 2);
}

void tst_QmlModels::dualCoreExplicitValueWins()
{
    {
        Settings s(iniPath());
        s.setDualCoreMode(true);
        QVERIFY(s.dualCoreMode());
    }
    {
        Settings s(iniPath());
        QVERIFY(s.dualCoreMode());
        s.setDualCoreMode(false);
        QVERIFY(!s.dualCoreMode());
    }
    // An explicit "off" sticks even though auto-detection might say on.
    Settings reader(iniPath());
    QVERIFY(!reader.dualCoreMode());
}

void tst_QmlModels::videoProfileFollowsDualCoreUnlessSet()
{
    Settings s(iniPath());

    s.setDualCoreMode(true);
    QCOMPARE(s.videoProfile(), QStringLiteral("fast"));

    s.setDualCoreMode(false);
    QCOMPARE(s.videoProfile(), QStringLiteral("balanced"));

    // An explicit profile overrides the dual-core default in either direction.
    s.setVideoProfile(QStringLiteral("high-quality"));
    QCOMPARE(s.videoProfile(), QStringLiteral("high-quality"));

    s.setDualCoreMode(true);
    QCOMPARE(s.videoProfile(), QStringLiteral("high-quality"));

    s.setVideoProfile(QStringLiteral("balanced"));
    QCOMPARE(s.videoProfile(), QStringLiteral("balanced"));
}

void tst_QmlModels::hwdecModeDefaultAndRoundTrip()
{
    Settings s(iniPath());
    QCOMPARE(s.hwdecMode(), QStringLiteral("auto-safe"));

    s.setHwdecMode(QStringLiteral("videotoolbox"));
    QCOMPARE(s.hwdecMode(), QStringLiteral("videotoolbox"));
}

void tst_QmlModels::maxBitrateDefaultsToAutoAndClampsNegative()
{
    Settings s(iniPath());
    QCOMPARE(s.maxBitrateMbps(), 0);

    s.setMaxBitrateMbps(25);
    QCOMPARE(s.maxBitrateMbps(), 25);

    // Negative input is stored as 0 (Auto), never as a negative cap.
    s.setMaxBitrateMbps(-3);
    QCOMPARE(s.maxBitrateMbps(), 0);
}

void tst_QmlModels::customMpvOptionsAndLogLevelDefaultsAndRoundTrip()
{
    {
        Settings defaults(iniPath());
        QCOMPARE(defaults.customMpvOptions(), QString());
        QCOMPARE(defaults.mpvLogLevel(), QStringLiteral("warn"));
    }

    Settings s(iniPath());
    s.setCustomMpvOptions(QStringLiteral("scale-antiring=0.5 deband=yes"));
    s.setMpvLogLevel(QStringLiteral("info"));

    QCOMPARE(s.customMpvOptions(), QStringLiteral("scale-antiring=0.5 deband=yes"));
    QCOMPARE(s.mpvLogLevel(), QStringLiteral("info"));
}

void tst_QmlModels::recentSearchesTrimDedupeAndMoveToFront()
{
    Settings s(iniPath());
    QVERIFY(s.recentSearches().isEmpty());

    s.addRecentSearch(QStringLiteral("  dune  "));
    QCOMPARE(s.recentSearches(), QStringList(QStringLiteral("dune")));

    // Blank and whitespace-only terms are ignored.
    s.addRecentSearch(QStringLiteral("   "));
    s.addRecentSearch(QString());
    QCOMPARE(s.recentSearches(), QStringList(QStringLiteral("dune")));

    s.addRecentSearch(QStringLiteral("alien"));
    QCOMPARE(s.recentSearches(), (QStringList{QStringLiteral("alien"), QStringLiteral("dune")}));

    // A repeat (after trimming) moves to the front instead of duplicating.
    s.addRecentSearch(QStringLiteral("dune"));
    QCOMPARE(s.recentSearches(), (QStringList{QStringLiteral("dune"), QStringLiteral("alien")}));

    s.addRecentSearch(QStringLiteral("  alien  "));
    QCOMPARE(s.recentSearches(), (QStringList{QStringLiteral("alien"), QStringLiteral("dune")}));
}

void tst_QmlModels::recentSearchesAreCappedAtTen()
{
    Settings s(iniPath());
    for (int i = 0; i < 12; ++i)
        s.addRecentSearch(QStringLiteral("term%1").arg(i));

    const QStringList searches = s.recentSearches();
    QCOMPARE(searches.size(), 10);
    // Newest first; the two oldest (term0, term1) have been dropped.
    QCOMPARE(searches.first(), QStringLiteral("term11"));
    QCOMPARE(searches.last(), QStringLiteral("term2"));
}

void tst_QmlModels::clearRecentSearchesRemovesPersistedList()
{
    {
        Settings writer(iniPath());
        writer.addRecentSearch(QStringLiteral("dune"));
        writer.addRecentSearch(QStringLiteral("alien"));
        QCOMPARE(writer.recentSearches().size(), 2);
        writer.clearRecentSearches();
        QVERIFY(writer.recentSearches().isEmpty());
    }

    Settings reader(iniPath());
    QVERIFY(reader.recentSearches().isEmpty());
}

void tst_QmlModels::windowStateDefaultsAndRoundTrip()
{
    {
        Settings defaults(iniPath());
        QCOMPARE(defaults.windowWidth(), 1340);
        QCOMPARE(defaults.windowHeight(), 840);
        QVERIFY(!defaults.windowMaximized());
    }

    {
        Settings writer(iniPath());
        writer.setWindowState(1600, 900, false);
    }
    {
        Settings reader(iniPath());
        QCOMPARE(reader.windowWidth(), 1600);
        QCOMPARE(reader.windowHeight(), 900);
        QVERIFY(!reader.windowMaximized());
    }

    // While maximized, the last windowed size is kept, not overwritten.
    Settings s(iniPath());
    s.setWindowState(2000, 1200, true);
    QCOMPARE(s.windowWidth(), 1600);
    QCOMPARE(s.windowHeight(), 900);
    QVERIFY(s.windowMaximized());

    s.setWindowState(1280, 800, false);
    QCOMPARE(s.windowWidth(), 1280);
    QCOMPARE(s.windowHeight(), 800);
    QVERIFY(!s.windowMaximized());
}

void tst_QmlModels::windowStateRejectsTooSmallSizes()
{
    Settings s(iniPath());
    s.setWindowState(1600, 900, false);

    // Below the 640x400 floor the size is ignored; the flag still applies.
    s.setWindowState(300, 200, false);
    QCOMPARE(s.windowWidth(), 1600);
    QCOMPARE(s.windowHeight(), 900);
    QVERIFY(!s.windowMaximized());
}

void tst_QmlModels::windowStateReadClampsToBounds()
{
    {
        QSettings raw(iniPath(), QSettings::IniFormat);
        raw.setValue(QStringLiteral("window/width"), 100);
        raw.setValue(QStringLiteral("window/height"), 99999);
        raw.sync();
    }
    {
        Settings s(iniPath());
        QCOMPARE(s.windowWidth(), 800);
        QCOMPARE(s.windowHeight(), 4320);
    }
    {
        QSettings raw(iniPath(), QSettings::IniFormat);
        raw.setValue(QStringLiteral("window/width"), 99999);
        raw.setValue(QStringLiteral("window/height"), 100);
        raw.sync();
    }
    Settings s(iniPath());
    QCOMPARE(s.windowWidth(), 7680);
    QCOMPARE(s.windowHeight(), 540);
}

QTEST_GUILESS_MAIN(tst_QmlModels)
#include "tst_qmlmodels.moc"
