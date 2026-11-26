#include "api/PlaybackNavigation.h"
#include "core/Formatting.h"

#include <QFile>
#include <QJsonDocument>
#include <QTest>

// Covers the pure decision logic behind skip-intro/credits and the
// next-episode countdown (src/api/PlaybackNavigation.h). This is the only
// part of either feature that gets verified in this project: the live
// Jellyfin 10.11.8 server used elsewhere in the test suite has no
// intro-detection plugin configured and returns an empty MediaSegments list
// for every item (FEATURES.md §B), so the actual "does a real segment make
// the corner button appear at the right moment" behaviour cannot be
// exercised here and needs a manual check against a server that has one.
//
// episodes.json/seasons.json are the same fixtures tst_models already uses
// for /Shows/{id}/Episodes and /Shows/{id}/Seasons -- real captured payloads
// for "The Kitchen (2012)", a 6-season, 20-episode-per-season series (the
// episodes fixture only captured the first page of 5 of season 1's 20).
class tst_PlaybackNavigation : public QObject
{
    Q_OBJECT
private slots:
    // decideSkipAffordance
    void skipEmptySegmentsIsNone();
    void skipInsideIntroIsActive();
    void skipAtIntroEndIsNone();
    void skipIgnoresUnknownSegmentTypes();
    void skipPicksTheSegmentCoveringThePosition();

    // decideCountdown
    void countdownNotVisibleForNonPositiveDuration();
    void countdownFallsBackToLast30SecondsWithoutSegments();
    void countdownUsesOutroStartWhenPresent();
    void countdownNotVisibleAtExactEnd();

    // nextEpisodeInSeason / firstEpisodeInSeason (episodes.json fixture)
    void nextEpisodeInSeasonFindsTheFollowingIndex();
    void nextEpisodeInSeasonSkipsGaps();
    void nextEpisodeInSeasonEmptyWhenNotFoundInList();
    void nextEpisodeInSeasonEmptyWhenCurrentHasNoIndex();
    void firstEpisodeInSeasonPicksLowestIndex();

    // nextSeasonId (seasons.json fixture)
    void nextSeasonIdFindsTheFollowingSeason();
    void nextSeasonIdEmptyForTheLastSeason();
    void nextSeasonIdEmptyWhenCurrentSeasonNotFound();

    // The specific edge case FEATURES.md calls out: last episode of the
    // last season must suppress the countdown chain entirely, at both
    // levels the resolution walks (in-season, then cross-season).
    void lastEpisodeOfLastSeasonHasNoNextEpisodeAnywhere();

private:
    static QList<MediaItem> loadItems(const QString &fixtureName)
    {
        QFile file(QStringLiteral(CUTEFIN_FIXTURES_DIR) + QLatin1Char('/') + fixtureName);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return parseItemList(QJsonDocument::fromJson(file.readAll()));
    }

    static MediaSegment makeSegment(const QString &type, qint64 startSeconds, qint64 endSeconds)
    {
        MediaSegment segment;
        segment.type = type;
        segment.startTicks = Formatting::secondsToTicks(startSeconds);
        segment.endTicks = Formatting::secondsToTicks(endSeconds);
        return segment;
    }

    static MediaItem episodeWithIndex(int index)
    {
        MediaItem item;
        item.indexNumber = index;
        return item;
    }
};

void tst_PlaybackNavigation::skipEmptySegmentsIsNone()
{
    // The normal "no intro-detection plugin" result (FEATURES.md §B) must
    // produce exactly the same outcome as "no segment at this position" --
    // there is deliberately no separate branch for it.
    const auto affordance = PlaybackNavigation::decideSkipAffordance({}, 30);
    QVERIFY(!affordance.isActive());
    QCOMPARE(affordance.kind, PlaybackNavigation::SegmentKind::None);
}

void tst_PlaybackNavigation::skipInsideIntroIsActive()
{
    const QList<MediaSegment> segments = {makeSegment(QStringLiteral("Intro"), 0, 90)};

    QVERIFY(!PlaybackNavigation::decideSkipAffordance(segments, -1).isActive());

    const auto atStart = PlaybackNavigation::decideSkipAffordance(segments, 0);
    QVERIFY(atStart.isActive());
    QCOMPARE(atStart.kind, PlaybackNavigation::SegmentKind::Intro);
    QCOMPARE(atStart.skipToSeconds, Q_INT64_C(90));

    const auto midway = PlaybackNavigation::decideSkipAffordance(segments, 45);
    QVERIFY(midway.isActive());
    QCOMPARE(midway.skipToSeconds, Q_INT64_C(90));
}

void tst_PlaybackNavigation::skipAtIntroEndIsNone()
{
    // Half-open [start, end): the affordance must disappear exactly when
    // playback reaches the point "Skip" would have sent it to, or clicking
    // skip and doing nothing would look identical to the user forever.
    const QList<MediaSegment> segments = {makeSegment(QStringLiteral("Intro"), 0, 90)};
    QVERIFY(!PlaybackNavigation::decideSkipAffordance(segments, 90).isActive());
    QVERIFY(!PlaybackNavigation::decideSkipAffordance(segments, 91).isActive());
}

void tst_PlaybackNavigation::skipIgnoresUnknownSegmentTypes()
{
    // Recap/Preview/Commercial are real Jellyfin segment types but have no
    // UI in this phase -- must never produce a skip affordance.
    const QList<MediaSegment> segments = {makeSegment(QStringLiteral("Recap"), 0, 30)};
    QVERIFY(!PlaybackNavigation::decideSkipAffordance(segments, 10).isActive());
}

void tst_PlaybackNavigation::skipPicksTheSegmentCoveringThePosition()
{
    // Outro listed first in the server's response, Intro second -- the
    // decision must be driven by which window actually contains the
    // position, not by list order.
    const QList<MediaSegment> segments = {
        makeSegment(QStringLiteral("Outro"), 1200, 1260),
        makeSegment(QStringLiteral("Intro"), 0, 60),
    };

    const auto duringIntro = PlaybackNavigation::decideSkipAffordance(segments, 30);
    QCOMPARE(duringIntro.kind, PlaybackNavigation::SegmentKind::Intro);
    QCOMPARE(duringIntro.skipToSeconds, Q_INT64_C(60));

    const auto duringOutro = PlaybackNavigation::decideSkipAffordance(segments, 1230);
    QCOMPARE(duringOutro.kind, PlaybackNavigation::SegmentKind::Outro);
    QCOMPARE(duringOutro.skipToSeconds, Q_INT64_C(1260));

    QVERIFY(!PlaybackNavigation::decideSkipAffordance(segments, 600).isActive());
}

void tst_PlaybackNavigation::countdownNotVisibleForNonPositiveDuration()
{
    // Duration not known yet (e.g. right after playItem(), before mpv's
    // duration property arrives) must never show the card.
    QVERIFY(!PlaybackNavigation::decideCountdown(0, 0, {}).visible);
    QVERIFY(!PlaybackNavigation::decideCountdown(5, -1, {}).visible);
}

void tst_PlaybackNavigation::countdownFallsBackToLast30SecondsWithoutSegments()
{
    const qint64 duration = 1200; // 20 minutes
    QVERIFY(!PlaybackNavigation::decideCountdown(duration - 31, duration, {}).visible);

    const auto atThreshold = PlaybackNavigation::decideCountdown(duration - 30, duration, {});
    QVERIFY(atThreshold.visible);
    QCOMPARE(atThreshold.secondsRemaining, Q_INT64_C(30));

    const auto nearEnd = PlaybackNavigation::decideCountdown(duration - 5, duration, {});
    QVERIFY(nearEnd.visible);
    QCOMPARE(nearEnd.secondsRemaining, Q_INT64_C(5));
}

void tst_PlaybackNavigation::countdownUsesOutroStartWhenPresent()
{
    const qint64 duration = 1200;
    // An Outro segment starting much earlier than the last 30s (a long
    // end-credits roll) must win over the fallback -- it is exact, the
    // fallback is a guess.
    const QList<MediaSegment> segments = {makeSegment(QStringLiteral("Outro"), 1100, duration)};

    QVERIFY(!PlaybackNavigation::decideCountdown(1099, duration, segments).visible);
    const auto atOutroStart = PlaybackNavigation::decideCountdown(1100, duration, segments);
    QVERIFY(atOutroStart.visible);
    QCOMPARE(atOutroStart.secondsRemaining, Q_INT64_C(100));
}

void tst_PlaybackNavigation::countdownNotVisibleAtExactEnd()
{
    const qint64 duration = 1200;
    const QList<MediaSegment> segments = {makeSegment(QStringLiteral("Outro"), 1100, duration)};
    // remaining must be strictly positive: at/after duration there is
    // nothing left to count down.
    QVERIFY(!PlaybackNavigation::decideCountdown(duration, duration, segments).visible);
}

void tst_PlaybackNavigation::nextEpisodeInSeasonFindsTheFollowingIndex()
{
    const QList<MediaItem> episodes = loadItems(QStringLiteral("episodes.json"));
    QCOMPARE(episodes.size(), 5);

    const MediaItem next = PlaybackNavigation::nextEpisodeInSeason(episodes, episodeWithIndex(3));
    QVERIFY(!next.id.isEmpty());
    QCOMPARE(next.indexNumber, 4);
    QCOMPARE(next.name, QStringLiteral("Episode 4"));
}

void tst_PlaybackNavigation::nextEpisodeInSeasonSkipsGaps()
{
    // Hand-built list with a missing episode 2 (unaired/deleted): "next"
    // after episode 1 must be the smallest index still greater than
    // current's, not "not found because index+1 is absent".
    QList<MediaItem> episodes = {episodeWithIndex(1), episodeWithIndex(3)};
    const MediaItem next = PlaybackNavigation::nextEpisodeInSeason(episodes, episodeWithIndex(1));
    QCOMPARE(next.indexNumber, 3);
}

void tst_PlaybackNavigation::nextEpisodeInSeasonEmptyWhenNotFoundInList()
{
    const QList<MediaItem> episodes = loadItems(QStringLiteral("episodes.json"));
    // The fixture only captured 5 of the season's 20 episodes; index 5 is
    // the last one present here, so within *this* list there is no next --
    // exactly the "last episode in season" contract, whether that is
    // because it truly is the last one or because of what got captured.
    const MediaItem next = PlaybackNavigation::nextEpisodeInSeason(episodes, episodeWithIndex(5));
    QVERIFY(next.id.isEmpty());
}

void tst_PlaybackNavigation::nextEpisodeInSeasonEmptyWhenCurrentHasNoIndex()
{
    const QList<MediaItem> episodes = loadItems(QStringLiteral("episodes.json"));
    const MediaItem next = PlaybackNavigation::nextEpisodeInSeason(episodes, MediaItem());
    QVERIFY(next.id.isEmpty());
}

void tst_PlaybackNavigation::firstEpisodeInSeasonPicksLowestIndex()
{
    const QList<MediaItem> episodes = loadItems(QStringLiteral("episodes.json"));
    const MediaItem first = PlaybackNavigation::firstEpisodeInSeason(episodes);
    QCOMPARE(first.indexNumber, 1);
    QCOMPARE(first.name, QStringLiteral("Episode 1"));

    QVERIFY(PlaybackNavigation::firstEpisodeInSeason({}).id.isEmpty());
}

void tst_PlaybackNavigation::nextSeasonIdFindsTheFollowingSeason()
{
    const QList<MediaItem> seasons = loadItems(QStringLiteral("seasons.json"));
    QCOMPARE(seasons.size(), 6);

    // Season 3's id, from the fixture.
    const QString season3Id = QStringLiteral("b2aa47e7a188070e82fcecc4a313c72b");
    const QString season4Id = QStringLiteral("cd919143831a1056b76cdf0d1240b2c3");
    QCOMPARE(PlaybackNavigation::nextSeasonId(seasons, season3Id), season4Id);
}

void tst_PlaybackNavigation::nextSeasonIdEmptyForTheLastSeason()
{
    const QList<MediaItem> seasons = loadItems(QStringLiteral("seasons.json"));
    const QString season6Id = QStringLiteral("c72005760d398cbca2f75ddee0478526"); // IndexNumber 6, the last one
    QVERIFY(PlaybackNavigation::nextSeasonId(seasons, season6Id).isEmpty());
}

void tst_PlaybackNavigation::nextSeasonIdEmptyWhenCurrentSeasonNotFound()
{
    const QList<MediaItem> seasons = loadItems(QStringLiteral("seasons.json"));
    QVERIFY(PlaybackNavigation::nextSeasonId(seasons, QStringLiteral("not-a-real-season-id")).isEmpty());
}

void tst_PlaybackNavigation::lastEpisodeOfLastSeasonHasNoNextEpisodeAnywhere()
{
    // A minimal single-season series, current playing its last episode.
    // This is the exact scenario the countdown must suppress: PlayerWindow
    // calls nextEpisodeInSeason() first (empty -- no next episode in this
    // season), then falls back to nextSeasonId() (also empty -- no season
    // after this one), and only then does it know for certain there is no
    // next episode at all and leaves m_nextEpisode empty forever.
    MediaItem season1;
    season1.id = QStringLiteral("season-1");
    season1.indexNumber = 1;
    const QList<MediaItem> seasons = {season1};

    MediaItem lastEpisode = episodeWithIndex(2);
    lastEpisode.id = QStringLiteral("ep-2");
    lastEpisode.seasonId = season1.id;
    const QList<MediaItem> episodesInSeason = {episodeWithIndex(1), lastEpisode};

    QVERIFY(PlaybackNavigation::nextEpisodeInSeason(episodesInSeason, lastEpisode).id.isEmpty());
    QVERIFY(PlaybackNavigation::nextSeasonId(seasons, season1.id).isEmpty());
}

QTEST_APPLESS_MAIN(tst_PlaybackNavigation)
#include "tst_playbacknavigation.moc"
