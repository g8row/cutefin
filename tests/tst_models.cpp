#include "api/Models.h"
#include "core/Formatting.h"

#include <QFile>
#include <QJsonDocument>
#include <QTest>

namespace {
QJsonDocument loadFixture(const QString &name)
{
    QFile file(QStringLiteral(CUTEFIN_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonDocument();
    return QJsonDocument::fromJson(file.readAll());
}
}

class tst_Models : public QObject
{
    Q_OBJECT
private slots:
    void movieItem();
    void bareArrayLatest();
    void envelopeResume();
    void userViews();
    void seasons();
    void episodes();
    void tickConversion();
};

void tst_Models::movieItem()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("item_movie.json"));
    QVERIFY(!doc.isNull());

    const MediaItem item = MediaItem::fromJson(doc.object());

    QCOMPARE(item.name, QStringLiteral("Us"));
    QCOMPARE(item.type, QStringLiteral("Movie"));
    QCOMPARE(item.id, QStringLiteral("b412df04b1e605e8356af78a0018447a"));
    QCOMPARE(item.productionYear, 2019);
    QCOMPARE(item.runTimeTicks, Q_INT64_C(70229440000));
    QCOMPARE(item.officialRating, QStringLiteral("R"));
    QCOMPARE(item.criticRating, 93.0);
    QVERIFY(qFuzzyCompare(item.communityRating, 6.942));

    QCOMPARE(item.genres.size(), 3);
    QVERIFY(item.genres.contains(QStringLiteral("Horror")));
    QVERIFY(item.genres.contains(QStringLiteral("Mystery")));
    QVERIFY(item.genres.contains(QStringLiteral("Thriller")));

    QCOMPARE(item.tagline, QStringLiteral("Watch yourself."));
    QVERIFY(!item.studios.isEmpty());
    QCOMPARE(item.studios.first().name, QStringLiteral("Monkeypaw Productions"));

    QVERIFY(!item.people.isEmpty());
    QCOMPARE(item.people.first().name, QStringLiteral("Lupita Nyong'o"));
    QCOMPARE(item.people.first().role, QStringLiteral("Adelaide Wilson / Red"));

    QCOMPARE(item.imageTags.value(QStringLiteral("Primary")), QStringLiteral("f5ab468bb79363920b6f8f42d484a7c7"));
    QCOMPARE(item.backdropTags.size(), 1);
    QCOMPARE(item.backdropTags.first(), QStringLiteral("5c51a380a0434e16ba9485951a2c9439"));

    QVERIFY(item.blurHashes.contains(QStringLiteral("Primary")));
    QCOMPARE(item.blurHashes.value(QStringLiteral("Primary")).value(QStringLiteral("f5ab468bb79363920b6f8f42d484a7c7")),
             QStringLiteral("d69$h2%M019F@sMxx^?b?H$+9tInM{V[xtxtEKIo$*%L"));

    QCOMPARE(item.userData.played, false);
    QCOMPARE(item.userData.playbackPositionTicks, Q_INT64_C(0));
    QCOMPARE(item.userData.isFavorite, false);
}

void tst_Models::bareArrayLatest()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("latest_array.json"));
    QVERIFY(!doc.isNull());
    QVERIFY(doc.isArray());

    const QList<MediaItem> items = parseItemList(doc);
    QVERIFY(!items.isEmpty());
    QCOMPARE(items.first().name, QStringLiteral("Us"));
    QCOMPARE(items.first().type, QStringLiteral("Movie"));
}

void tst_Models::envelopeResume()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("resume_envelope.json"));
    QVERIFY(!doc.isNull());
    QVERIFY(doc.isObject());
    QVERIFY(doc.object().contains(QStringLiteral("Items")));

    const QList<MediaItem> items = parseItemList(doc);
    QVERIFY(!items.isEmpty());

    const MediaItem &episode = items.first();
    QCOMPARE(episode.type, QStringLiteral("Episode"));
    QCOMPARE(episode.seriesName, QStringLiteral("The Kitchen (2012)"));
    QCOMPARE(episode.seasonName, QStringLiteral("Season 1"));
    QCOMPARE(episode.indexNumber, 1);
    QCOMPARE(episode.parentIndexNumber, 1);
    QVERIFY(episode.userData.playbackPositionTicks > 0);
    QVERIFY(episode.userData.playedPercentage > 0.0);
}

void tst_Models::userViews()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("userviews.json"));
    QVERIFY(!doc.isNull());

    const QList<MediaItem> items = parseItemList(doc);
    QCOMPARE(items.size(), 4);

    bool foundMovies = false;
    for (const MediaItem &item : items) {
        if (item.name == QStringLiteral("Movies")) {
            foundMovies = true;
            QCOMPARE(item.collectionType, QStringLiteral("movies"));
        }
    }
    QVERIFY(foundMovies);
}

void tst_Models::seasons()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("seasons.json"));
    QVERIFY(!doc.isNull());

    const QList<MediaItem> items = parseItemList(doc);
    QVERIFY(!items.isEmpty());
    QCOMPARE(items.first().name, QStringLiteral("Season 1"));
    QCOMPARE(items.first().type, QStringLiteral("Season"));
    QCOMPARE(items.first().seriesName, QStringLiteral("The Kitchen (2012)"));
}

void tst_Models::episodes()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("episodes.json"));
    QVERIFY(!doc.isNull());

    const QList<MediaItem> items = parseItemList(doc);
    QVERIFY(!items.isEmpty());

    const MediaItem &ep = items.first();
    QCOMPARE(ep.name, QStringLiteral("Episode 1"));
    QCOMPARE(ep.type, QStringLiteral("Episode"));
    QCOMPARE(ep.seriesId, QStringLiteral("d4cb0178dcbb72981387326ccb2829cc"));
    QCOMPARE(ep.seasonId, QStringLiteral("8b40cf3e5c6f67541ed04320744e192d"));
    QCOMPARE(ep.runTimeTicks, Q_INT64_C(15148870000));
}

void tst_Models::tickConversion()
{
    // 70229440000 ticks (100ns units) == 7022 seconds == 1h 57m.
    QCOMPARE(Formatting::ticksToSeconds(70229440000LL), Q_INT64_C(7022));
    QCOMPARE(Formatting::secondsToTicks(7022LL), Q_INT64_C(70220000000));
    QCOMPARE(Formatting::formatRuntimeTicks(70229440000LL), QStringLiteral("1h 57m"));
}

QTEST_APPLESS_MAIN(tst_Models)
#include "tst_models.moc"
