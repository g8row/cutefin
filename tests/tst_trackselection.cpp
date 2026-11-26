#include "core/TrackSelection.h"

#include <QTest>

using TrackSelection::RememberedChoice;
using TrackSelection::TrackDescriptor;

// Covers the pure matching logic behind "remembered track choices across
// episodes of a series" (FEATURES.md §C2, src/core/TrackSelection.h). This is
// the only part of that feature that can be exercised without a live mpv
// instance and a real multi-episode file set -- "does picking a track in the
// menu actually stick across S1E1 -> S1E2" needs the manual check described
// in the phase-3b report.
class tst_TrackSelection : public QObject
{
    Q_OBJECT
private slots:
    void pickEmptyListIsAlwaysMinusOne();
    void pickNoRememberedChoiceFallsBackToPreferredLanguage();
    void pickSameLanguageDifferentCodecStillMatchesOnLanguage();
    void pickPrefersForcedMatchOverForcedMismatch();
    void pickPrefersSdhMatchOverSdhMismatch();
    void pickRememberedLanguageMissingFallsBackToPreferredLanguage();
    void pickRememberedLanguageMissingAndNoPreferredReturnsMinusOne();
    void pickExternalTrackMatchesLikeAnyOther();
    void pickTieBreaksByLowestId();
    void choiceForTrackCapturesAttributesAndSetsHasChoice();
    void spdifOptionValueListsExactlyTheSpecCodecs();
    void canPassthroughIsCaseInsensitiveAndCodecSpecific();

private:
    static TrackDescriptor track(int id, const QString &lang, const QString &codec, bool forced = false,
                                  bool hi = false, bool external = false)
    {
        TrackDescriptor t;
        t.id = id;
        t.language = lang;
        t.codec = codec;
        t.forced = forced;
        t.hearingImpaired = hi;
        t.external = external;
        return t;
    }

    static RememberedChoice remembered(const QString &lang, const QString &codec, bool forced = false,
                                        bool hi = false)
    {
        RememberedChoice choice;
        choice.language = lang;
        choice.codec = codec;
        choice.forced = forced;
        choice.hearingImpaired = hi;
        choice.hasChoice = true;
        return choice;
    }
};

void tst_TrackSelection::pickEmptyListIsAlwaysMinusOne()
{
    QCOMPARE(TrackSelection::pickTrack({}, remembered(QStringLiteral("eng"), QStringLiteral("aac")),
                                        QStringLiteral("eng")),
             -1);
    QCOMPARE(TrackSelection::pickTrack({}, RememberedChoice(), QString()), -1);
}

void tst_TrackSelection::pickNoRememberedChoiceFallsBackToPreferredLanguage()
{
    // Brand-new series: nothing remembered yet (hasChoice == false), so the
    // global preferred-language setting decides.
    const QList<TrackDescriptor> tracks = {
        track(1, QStringLiteral("jpn"), QStringLiteral("aac")),
        track(2, QStringLiteral("eng"), QStringLiteral("aac")),
    };
    QCOMPARE(TrackSelection::pickTrack(tracks, RememberedChoice(), QStringLiteral("eng")), 2);
    // No preferred language either -- leave mpv's own default alone.
    QCOMPARE(TrackSelection::pickTrack(tracks, RememberedChoice(), QString()), -1);
}

void tst_TrackSelection::pickSameLanguageDifferentCodecStillMatchesOnLanguage()
{
    // The remembered choice was an AAC English track; this release only has
    // E-AC3 English. Matching must degrade to language alone rather than
    // falling all the way through to the preferred-language setting (which
    // here would even pick a *different* track) -- a codec change between
    // rips of the same show must not look like "the remembered track is
    // gone".
    const QList<TrackDescriptor> tracks = {
        track(1, QStringLiteral("jpn"), QStringLiteral("aac")),
        track(2, QStringLiteral("eng"), QStringLiteral("eac3")),
    };
    const RememberedChoice choice = remembered(QStringLiteral("eng"), QStringLiteral("aac"));
    QCOMPARE(TrackSelection::pickTrack(tracks, choice, QStringLiteral("jpn")), 2);
}

void tst_TrackSelection::pickPrefersForcedMatchOverForcedMismatch()
{
    // Two English subtitle tracks: a forced (signs-only) one and a full one.
    // The remembered choice was forced -- it must win even though both share
    // language and codec, i.e. forced-ness must actually affect the score,
    // not just be along for the ride.
    const QList<TrackDescriptor> tracks = {
        track(1, QStringLiteral("eng"), QStringLiteral("subrip"), /*forced=*/false),
        track(2, QStringLiteral("eng"), QStringLiteral("subrip"), /*forced=*/true),
    };
    const RememberedChoice choice = remembered(QStringLiteral("eng"), QStringLiteral("subrip"), /*forced=*/true);
    QCOMPARE(TrackSelection::pickTrack(tracks, choice, QString()), 2);
}

void tst_TrackSelection::pickPrefersSdhMatchOverSdhMismatch()
{
    const QList<TrackDescriptor> tracks = {
        track(1, QStringLiteral("eng"), QStringLiteral("subrip"), false, /*hi=*/false),
        track(2, QStringLiteral("eng"), QStringLiteral("subrip"), false, /*hi=*/true),
    };
    const RememberedChoice choice =
        remembered(QStringLiteral("eng"), QStringLiteral("subrip"), /*forced=*/false, /*hi=*/true);
    QCOMPARE(TrackSelection::pickTrack(tracks, choice, QString()), 2);
}

void tst_TrackSelection::pickRememberedLanguageMissingFallsBackToPreferredLanguage()
{
    // The remembered language (Spanish) doesn't exist in this file at all --
    // degrade to the preferred-language setting rather than leaving nothing
    // selected.
    const QList<TrackDescriptor> tracks = {
        track(1, QStringLiteral("jpn"), QStringLiteral("aac")),
        track(2, QStringLiteral("eng"), QStringLiteral("aac")),
    };
    const RememberedChoice choice = remembered(QStringLiteral("spa"), QStringLiteral("aac"));
    QCOMPARE(TrackSelection::pickTrack(tracks, choice, QStringLiteral("eng")), 2);
}

void tst_TrackSelection::pickRememberedLanguageMissingAndNoPreferredReturnsMinusOne()
{
    // Never leave the user with no audio (Phase 3b requirement 2): when
    // neither the remembered choice nor the preferred-language setting can
    // be satisfied, defer to mpv's own default rather than forcing a track.
    const QList<TrackDescriptor> tracks = {track(1, QStringLiteral("jpn"), QStringLiteral("aac"))};
    const RememberedChoice choice = remembered(QStringLiteral("spa"), QStringLiteral("aac"));
    QCOMPARE(TrackSelection::pickTrack(tracks, choice, QString()), -1);
}

void tst_TrackSelection::pickExternalTrackMatchesLikeAnyOther()
{
    // A remembered SDH English subtitle must be found whether this release
    // ships it embedded or as an external sidecar file -- `external` is
    // informational only and must not gate matching.
    const QList<TrackDescriptor> tracks = {
        track(1, QStringLiteral("eng"), QStringLiteral("ass"), false, false, /*external=*/false),
        track(2, QStringLiteral("eng"), QStringLiteral("subrip"), false, /*hi=*/true, /*external=*/true),
    };
    const RememberedChoice choice =
        remembered(QStringLiteral("eng"), QStringLiteral("subrip"), /*forced=*/false, /*hi=*/true);
    QCOMPARE(TrackSelection::pickTrack(tracks, choice, QString()), 2);
}

void tst_TrackSelection::pickTieBreaksByLowestId()
{
    // Both tracks score identically against the remembered choice (same
    // language, neither matches its codec, both mismatch forced/SDH the same
    // way) -- the result must be deterministic, not "whichever the loop
    // happens to see last".
    const QList<TrackDescriptor> tracks = {
        track(5, QStringLiteral("eng"), QStringLiteral("ac3")),
        track(3, QStringLiteral("eng"), QStringLiteral("dts")),
    };
    const RememberedChoice choice = remembered(QStringLiteral("eng"), QStringLiteral("flac"));
    QCOMPARE(TrackSelection::pickTrack(tracks, choice, QString()), 3);
}

void tst_TrackSelection::choiceForTrackCapturesAttributesAndSetsHasChoice()
{
    const TrackDescriptor t = track(7, QStringLiteral("eng"), QStringLiteral("eac3"), true, true, false);
    const RememberedChoice choice = TrackSelection::choiceForTrack(t);
    QVERIFY(choice.hasChoice);
    QCOMPARE(choice.language, QStringLiteral("eng"));
    QCOMPARE(choice.codec, QStringLiteral("eac3"));
    QVERIFY(choice.forced);
    QVERIFY(choice.hearingImpaired);
}

void tst_TrackSelection::spdifOptionValueListsExactlyTheSpecCodecs()
{
    QCOMPARE(TrackSelection::spdifOptionValue(true), QStringLiteral("ac3,dts,eac3"));
    QVERIFY(TrackSelection::spdifOptionValue(false).isEmpty());
}

void tst_TrackSelection::canPassthroughIsCaseInsensitiveAndCodecSpecific()
{
    QVERIFY(TrackSelection::canPassthrough(QStringLiteral("ac3")));
    QVERIFY(TrackSelection::canPassthrough(QStringLiteral("DTS")));
    QVERIFY(TrackSelection::canPassthrough(QStringLiteral("eac3")));
    QVERIFY(!TrackSelection::canPassthrough(QStringLiteral("aac")));
    QVERIFY(!TrackSelection::canPassthrough(QStringLiteral("flac")));
    QVERIFY(!TrackSelection::canPassthrough(QString()));
}

QTEST_APPLESS_MAIN(tst_TrackSelection)
#include "tst_trackselection.moc"
