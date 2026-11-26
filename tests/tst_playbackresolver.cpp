#include "api/JellyfinClient.h"
#include "api/Models.h"
#include "core/Formatting.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrlQuery>

namespace {
QJsonDocument loadFixture(const QString &name)
{
    QFile file(QStringLiteral(CUTEFIN_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonDocument();
    return QJsonDocument::fromJson(file.readAll());
}

QList<MediaSource> sourcesFromFixture(const QJsonDocument &doc)
{
    QList<MediaSource> sources;
    for (const QJsonValue &v : doc.object().value(QStringLiteral("MediaSources")).toArray())
        sources.append(MediaSource::fromJson(v.toObject()));
    return sources;
}
}

class tst_PlaybackResolver : public QObject
{
    Q_OBJECT
private slots:
    void fixtureDirectPlayBuildsStreamUrlWithApiKey();
    void fixtureTranscodeFallsBackToTranscodingUrl();
    void noPlayableSourceIsReportedAsNotOk();
    void tickConversionHelpersRoundTrip();
};

// playbackinfo_directplay.json was captured live against a permissive
// DeviceProfile (DESIGN.md §3): the server returns SupportsDirectPlay: true
// and SupportsDirectStream: true for the h264/eac3 mkv test source, with no
// TranscodingUrl. This exercises the direct-play branch of resolvePlayback().
void tst_PlaybackResolver::fixtureDirectPlayBuildsStreamUrlWithApiKey()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("playbackinfo_directplay.json"));
    QVERIFY(!doc.isNull());

    // PlaySessionId is minted fresh by the server on every PlaybackInfo call,
    // so its value is not stable across captures -- only assert it is present.
    const QString playSessionId = doc.object().value(QStringLiteral("PlaySessionId")).toString();
    QVERIFY(!playSessionId.isEmpty());

    const QList<MediaSource> sources = sourcesFromFixture(doc);
    QCOMPARE(sources.size(), 1);
    const MediaSource &source = sources.first();
    QCOMPARE(source.id, QStringLiteral("b412df04b1e605e8356af78a0018447a"));
    QCOMPARE(source.container, QStringLiteral("mkv"));
    QVERIFY(source.supportsDirectPlay);
    QVERIFY(source.supportsDirectStream);
    QVERIFY(source.transcodingUrl.isEmpty());

    const auto resolution = JellyfinClient::resolvePlayback(playSessionId, sources,
                                                              QStringLiteral("https://jelly.example.com"),
                                                              QStringLiteral("token-abc"));

    QVERIFY(resolution.ok);
    QVERIFY(!resolution.isTranscoding);
    QCOMPARE(resolution.mediaSourceId, source.id);
    // Session id must be propagated verbatim from whatever the caller passed in.
    QCOMPARE(resolution.playSessionId, playSessionId);
    QCOMPARE(resolution.url.path(), QStringLiteral("/Videos/%1/stream").arg(source.id));

    QUrlQuery query(resolution.url);
    QCOMPARE(query.queryItemValue(QStringLiteral("static")), QStringLiteral("true"));
    QCOMPARE(query.queryItemValue(QStringLiteral("mediaSourceId")), source.id);
    QCOMPARE(query.queryItemValue(QStringLiteral("api_key")), QStringLiteral("token-abc"));
}

// playbackinfo_transcode.json was captured live against a deliberately
// restrictive DeviceProfile (webm/vp9/opus only, 3 Mbps cap) that forces the
// server to reject direct play/stream and hand back an HLS TranscodingUrl.
// This exercises the transcode-fallback branch of resolvePlayback().
void tst_PlaybackResolver::fixtureTranscodeFallsBackToTranscodingUrl()
{
    const QJsonDocument doc = loadFixture(QStringLiteral("playbackinfo_transcode.json"));
    QVERIFY(!doc.isNull());

    const QString playSessionId = doc.object().value(QStringLiteral("PlaySessionId")).toString();
    QVERIFY(!playSessionId.isEmpty());

    const QList<MediaSource> sources = sourcesFromFixture(doc);
    QCOMPARE(sources.size(), 1);
    const MediaSource &source = sources.first();
    QVERIFY(!source.supportsDirectPlay);
    QVERIFY(!source.supportsDirectStream);
    QVERIFY(!source.transcodingUrl.isEmpty());
    QVERIFY(source.transcodingUrl.contains(QStringLiteral("master.m3u8")));

    const auto resolution = JellyfinClient::resolvePlayback(playSessionId, sources,
                                                              QStringLiteral("https://jelly.example.com"),
                                                              QStringLiteral("token-abc"));

    QVERIFY(resolution.ok);
    QVERIFY(resolution.isTranscoding);
    QCOMPARE(resolution.mediaSourceId, source.id);
    QCOMPARE(resolution.playSessionId, playSessionId);
    // The resolver must not touch the TranscodingUrl's own query string --
    // it already carries mediaSourceId/api_key/PlaySessionId etc (DESIGN.md §3).
    QCOMPARE(resolution.url.toString(), QStringLiteral("https://jelly.example.com") + source.transcodingUrl);
}

// This case has no live equivalent -- a real server always advertises either
// direct play/stream or a transcoding fallback for a requestable item, so a
// source with neither is hand-built here purely to exercise resolvePlayback's
// "nothing playable" failure path.
void tst_PlaybackResolver::noPlayableSourceIsReportedAsNotOk()
{
    MediaSource source;
    source.id = QStringLiteral("dead-end");
    source.supportsDirectPlay = false;
    source.supportsDirectStream = false;
    // transcodingUrl left empty: nothing the resolver can do.

    const auto resolution = JellyfinClient::resolvePlayback(QStringLiteral("session-789"), {source},
                                                              QStringLiteral("https://jelly.example.com"),
                                                              QStringLiteral("token-abc"));
    QVERIFY(!resolution.ok);
    QVERIFY(resolution.url.isEmpty());
}

// PositionTicks are 100ns units (DESIGN.md §3): a factor-of-10^7 error here
// is the classic bug that lands resume positions in the wrong place.
void tst_PlaybackResolver::tickConversionHelpersRoundTrip()
{
    QCOMPARE(Formatting::secondsToTicks(1LL), Q_INT64_C(10'000'000));
    QCOMPARE(Formatting::ticksToSeconds(10'000'000LL), Q_INT64_C(1));

    const qint64 seconds = 754;
    QCOMPARE(Formatting::ticksToSeconds(Formatting::secondsToTicks(seconds)), seconds);
}

QTEST_APPLESS_MAIN(tst_PlaybackResolver)
#include "tst_playbackresolver.moc"
