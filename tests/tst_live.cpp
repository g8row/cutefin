#include "api/JellyfinClient.h"
#include "core/ImageCache.h"
#include "core/NetworkQueue.h"
#include "core/Settings.h"

#include <QEventLoop>
#include <QTest>
#include <QTimer>
#include <QUuid>

class tst_Live : public QObject
{
    Q_OBJECT
private slots:
    void fullServerIntegration();
};

void tst_Live::fullServerIntegration()
{
    const QString rawServer = QString::fromUtf8(qgetenv("CUTEFIN_TEST_SERVER")).trimmed();
    const QString user = QString::fromUtf8(qgetenv("CUTEFIN_TEST_USER")).trimmed();
    const QString pass = QString::fromUtf8(qgetenv("CUTEFIN_TEST_PASS"));

    if (rawServer.isEmpty() || user.isEmpty()) {
        QSKIP("Set CUTEFIN_TEST_SERVER, CUTEFIN_TEST_USER, CUTEFIN_TEST_PASS to run live server tests.");
    }

    QString serverUrl = rawServer;
    if (!serverUrl.contains(QStringLiteral("://")))
        serverUrl.prepend(QStringLiteral("https://"));
    while (serverUrl.endsWith(QLatin1Char('/')))
        serverUrl.chop(1);

    NetworkQueue queue;
    JellyfinClient client(&queue);
    client.setDeviceId(QUuid::createUuid().toString(QUuid::WithoutBraces));
    client.setClientVersion(QStringLiteral("0.2.0-test"));

    auto waitFor = [](const std::function<void(std::function<void()>)> &op, int timeoutMs = 15000) -> bool {
        QEventLoop loop;
        bool finished = false;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        op([&]() {
            finished = true;
            loop.quit();
        });
        if (!finished)
            loop.exec();
        return finished;
    };

    // 1. Probe server
    bool probeOk = false;
    ServerInfo info;
    QString err;
    QVERIFY(waitFor([&](std::function<void()> done) {
        client.probeServer(serverUrl, this, [&, done](bool ok, const ServerInfo &si, const QString &e) {
            probeOk = ok;
            info = si;
            err = e;
            done();
        });
    }));
    QVERIFY2(probeOk, qPrintable(QStringLiteral("probeServer failed: %1").arg(err)));
    QVERIFY(!info.name.isEmpty());
    qInfo() << "Connected to Jellyfin:" << info.name << "v" << info.version;

    // 2. Login
    client.setServerUrl(serverUrl);
    bool loginOk = false;
    UserSession session;
    QVERIFY(waitFor([&](std::function<void()> done) {
        client.login(user, pass, this, [&, done](bool ok, const UserSession &s, const QString &e) {
            loginOk = ok;
            session = s;
            err = e;
            done();
        });
    }));
    QVERIFY2(loginOk && session.isValid(), qPrintable(QStringLiteral("login failed: %1").arg(err)));
    client.setSession(session);

    // 3. Fetch UserViews
    bool viewsOk = false;
    QList<MediaItem> views;
    QVERIFY(waitFor([&](std::function<void()> done) {
        client.fetchUserViews(this, [&, done](bool ok, const QList<MediaItem> &items, const QString &e) {
            viewsOk = ok;
            views = items;
            err = e;
            done();
        });
    }));
    QVERIFY2(viewsOk, qPrintable(err));
    QVERIFY(!views.isEmpty());
    qInfo() << "Libraries count:" << views.size();

    // 4. Fetch Resume + NextUp + Latest in first video library
    bool resumeOk = false;
    QList<MediaItem> resumeItems;
    QVERIFY(waitFor([&](std::function<void()> done) {
        client.fetchResumeItems(this, [&, done](bool ok, const QList<MediaItem> &items, const QString &e) {
            resumeOk = ok;
            resumeItems = items;
            err = e;
            done();
        });
    }));
    QVERIFY2(resumeOk, qPrintable(err));

    QString sampleItemId;
    QString samplePrimaryTag;
    for (const MediaItem &it : resumeItems) {
        if (it.type == QLatin1String("Movie") || it.type == QLatin1String("Episode")) {
            sampleItemId = it.id;
            samplePrimaryTag = it.imageTags.value(QStringLiteral("Primary"));
            break;
        }
    }

    // 5. Paged library browse + filters
    QString videoViewId;
    for (const MediaItem &v : views) {
        if (v.collectionType == QLatin1String("movies") || v.collectionType == QLatin1String("tvshows")) {
            videoViewId = v.id;
            break;
        }
    }
    if (videoViewId.isEmpty())
        videoViewId = views.first().id;

    bool pageOk = false;
    JellyfinClient::ItemPage page;
    JellyfinClient::ItemQuery query;
    query.parentId = videoViewId;
    query.limit = 10;
    QVERIFY(waitFor([&](std::function<void()> done) {
        client.fetchItems(query, this, [&, done](bool ok, const JellyfinClient::ItemPage &p, const QString &e) {
            pageOk = ok;
            page = p;
            err = e;
            done();
        });
    }));
    QVERIFY2(pageOk, qPrintable(err));
    qInfo() << "Library sample page items:" << page.items.size() << "total:" << page.totalRecordCount;

    if (sampleItemId.isEmpty()) {
        for (const MediaItem &it : page.items) {
            if (it.type == QLatin1String("Movie") || it.type == QLatin1String("Episode")) {
                sampleItemId = it.id;
                samplePrimaryTag = it.imageTags.value(QStringLiteral("Primary"));
                break;
            }
        }
    }

    // 6. ImageCache off-thread fetch & QImage decode
    if (!sampleItemId.isEmpty() && !samplePrimaryTag.isEmpty()) {
        ImageCache imageCache(&queue);
        imageCache.setServerUrl(serverUrl);
        imageCache.setAccessToken(session.token);
        QImage fetchedImg;
        QVERIFY(waitFor([&](std::function<void()> done) {
            imageCache.requestImage(sampleItemId, ImageCache::ImageType::Primary, samplePrimaryTag, 300, this,
                                    [&, done](const QImage &img) {
                                        fetchedImg = img;
                                        done();
                                    });
        }));
        QVERIFY2(!fetchedImg.isNull(), "ImageCache failed to fetch/decode primary poster");
        qInfo() << "Fetched poster QImage:" << fetchedImg.size();
    }

    // 7. PlaybackInfo resolution + ItemDetails on a playable item
    if (!sampleItemId.isEmpty()) {
        bool detailsOk = false;
        MediaItem fullItem;
        QVERIFY(waitFor([&](std::function<void()> done) {
            client.fetchItemDetails(sampleItemId, this, [&, done](bool ok, const MediaItem &it, const QString &e) {
                detailsOk = ok;
                fullItem = it;
                err = e;
                done();
            });
        }));
        QVERIFY2(detailsOk, qPrintable(err));

        JellyfinClient::PlaybackResolution res;
        QVERIFY(waitFor([&](std::function<void()> done) {
            client.getPlaybackInfo(sampleItemId, this,
                                   [&, done](const JellyfinClient::PlaybackResolution &r, const QString &e) {
                                       res = r;
                                       err = e;
                                       done();
                                   });
        }));
        QVERIFY2(res.ok && res.url.isValid(), qPrintable(QStringLiteral("PlaybackInfo failed: %1").arg(err)));
        qInfo() << "Playback resolved:" << (res.isTranscoding ? "Transcode" : "DirectPlay")
                << "container:" << res.container << "chapters:" << fullItem.chapters.size()
                << "trickplay:" << !fullItem.trickplay.isEmpty();
    }

    // 8. Logout cleanly
    QVERIFY(waitFor([&](std::function<void()> done) { client.logout(this, done); }));
}

QTEST_GUILESS_MAIN(tst_Live)
#include "tst_live.moc"
