#include "api/JellyfinClient.h"
#include "core/NetworkQueue.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

// Covers the pure query-building and filter-parsing logic behind the library
// grid. No network: buildItemsPath() and parseLibraryFilters() are the two
// places where a wrong separator or a missing parameter silently produces the
// wrong result set rather than an error, so they are worth pinning down.
class TestItemQuery : public QObject
{
    Q_OBJECT

private:
    // Parses the path buildItemsPath() produced back into a lookup table so
    // assertions read as "this parameter has this value" rather than string
    // matching against a particular parameter ordering.
    static QUrlQuery queryOf(const QString &path)
    {
        const int mark = path.indexOf(QLatin1Char('?'));
        return QUrlQuery(path.mid(mark + 1));
    }

private slots:
    void defaultsAreSaneAndPaged();
    void filtersUseTheSeparatorsJellyfinExpects();
    void playedIsTriState();
    void favoritesAndPersonAndSearch();
    void parsesFilters();
    void parsesFiltersFromEmptyPayload();
    void trickplayUsesAuthorizationHeader();
};

void TestItemQuery::defaultsAreSaneAndPaged()
{
    NetworkQueue queue;
    JellyfinClient client(&queue);
    UserSession session;
    session.serverUrl = QStringLiteral("https://example.test");
    session.userId = QStringLiteral("uid-1");
    session.token = QStringLiteral("tok");
    client.setSession(session);

    JellyfinClient::ItemQuery query;
    query.parentId = QStringLiteral("lib-1");
    query.includeItemTypes = {QStringLiteral("Movie")};
    query.startIndex = 200;
    query.limit = 100;

    const QUrlQuery q = queryOf(client.buildItemsPath(query));
    QCOMPARE(q.queryItemValue(QStringLiteral("userId")), QStringLiteral("uid-1"));
    QCOMPARE(q.queryItemValue(QStringLiteral("parentId")), QStringLiteral("lib-1"));
    QCOMPARE(q.queryItemValue(QStringLiteral("includeItemTypes")), QStringLiteral("Movie"));
    QCOMPARE(q.queryItemValue(QStringLiteral("recursive")), QStringLiteral("true"));
    QCOMPARE(q.queryItemValue(QStringLiteral("startIndex")), QStringLiteral("200"));
    QCOMPARE(q.queryItemValue(QStringLiteral("limit")), QStringLiteral("100"));
    QCOMPARE(q.queryItemValue(QStringLiteral("sortBy")), QStringLiteral("SortName"));
    QCOMPARE(q.queryItemValue(QStringLiteral("sortOrder")), QStringLiteral("Ascending"));

    // Absent filters must not appear at all -- an empty genres= would filter
    // the result set down to nothing rather than being ignored.
    QVERIFY(!q.hasQueryItem(QStringLiteral("genres")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("isPlayed")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("isFavorite")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("personIds")));
}

void TestItemQuery::filtersUseTheSeparatorsJellyfinExpects()
{
    NetworkQueue queue;
    JellyfinClient client(&queue);

    JellyfinClient::ItemQuery query;
    query.genres = {QStringLiteral("Horror"), QStringLiteral("Science Fiction")};
    query.officialRatings = {QStringLiteral("R"), QStringLiteral("PG-13")};
    query.years = {1999, 2001};
    query.ascending = false;
    query.sortBy = QStringLiteral("CommunityRating");

    const QString path = client.buildItemsPath(query);
    const QUrlQuery q = queryOf(path);

    // Genres and ratings are pipe-separated in ONE parameter; repeating the
    // parameter instead makes Jellyfin keep only the last value.
    QCOMPARE(q.queryItemValue(QStringLiteral("genres"), QUrl::FullyDecoded),
             QStringLiteral("Horror|Science Fiction"));
    QCOMPARE(q.queryItemValue(QStringLiteral("officialRatings"), QUrl::FullyDecoded),
             QStringLiteral("R|PG-13"));
    // Years are comma-separated, unlike the two above.
    QCOMPARE(q.queryItemValue(QStringLiteral("years"), QUrl::FullyDecoded), QStringLiteral("1999,2001"));
    QCOMPARE(q.queryItemValue(QStringLiteral("sortOrder")), QStringLiteral("Descending"));
    QCOMPARE(q.queryItemValue(QStringLiteral("sortBy")), QStringLiteral("CommunityRating"));

    // The space in "Science Fiction" has to survive as an encoded space, not
    // become a parameter break.
    QVERIFY(!path.contains(QStringLiteral("Science Fiction")));
}

void TestItemQuery::playedIsTriState()
{
    NetworkQueue queue;
    JellyfinClient client(&queue);

    JellyfinClient::ItemQuery query;

    query.played = JellyfinClient::ItemQuery::Played::Any;
    QVERIFY(!queryOf(client.buildItemsPath(query)).hasQueryItem(QStringLiteral("isPlayed")));

    query.played = JellyfinClient::ItemQuery::Played::UnplayedOnly;
    QCOMPARE(queryOf(client.buildItemsPath(query)).queryItemValue(QStringLiteral("isPlayed")),
             QStringLiteral("false"));

    query.played = JellyfinClient::ItemQuery::Played::PlayedOnly;
    QCOMPARE(queryOf(client.buildItemsPath(query)).queryItemValue(QStringLiteral("isPlayed")),
             QStringLiteral("true"));
}

void TestItemQuery::favoritesAndPersonAndSearch()
{
    NetworkQueue queue;
    JellyfinClient client(&queue);

    JellyfinClient::ItemQuery query;
    query.favoritesOnly = true;
    query.personId = QStringLiteral("person-9");
    query.searchTerm = QStringLiteral("blade runner");
    query.nameStartsWith = QStringLiteral("B");

    const QUrlQuery q = queryOf(client.buildItemsPath(query));
    QCOMPARE(q.queryItemValue(QStringLiteral("isFavorite")), QStringLiteral("true"));
    QCOMPARE(q.queryItemValue(QStringLiteral("personIds")), QStringLiteral("person-9"));
    QCOMPARE(q.queryItemValue(QStringLiteral("searchTerm"), QUrl::FullyDecoded), QStringLiteral("blade runner"));
    QCOMPARE(q.queryItemValue(QStringLiteral("nameStartsWith")), QStringLiteral("B"));
}

void TestItemQuery::parsesFilters()
{
    // Shape captured from a real /Items/Filters response on 10.11.8. Note it
    // is /Items/Filters and not /Items/Filters2: only the older endpoint
    // reports OfficialRatings and Years, which the filter menu needs.
    const QByteArray payload = R"({
        "Genres": ["Action", "Adventure", "Horror"],
        "Tags": ["1830s"],
        "OfficialRatings": ["NR", "PG", "R"],
        "Years": [1999, 2020, 1962]
    })";

    const JellyfinClient::LibraryFilters filters =
        JellyfinClient::parseLibraryFilters(QJsonDocument::fromJson(payload).object());

    QCOMPARE(filters.genres.size(), 3);
    QCOMPARE(filters.genres.first(), QStringLiteral("Action"));
    QCOMPARE(filters.officialRatings.size(), 3);
    QVERIFY(filters.officialRatings.contains(QStringLiteral("PG")));

    // Newest year first: a filter menu listing 1901 before 2024 is unusable.
    QCOMPARE(filters.years, (QList<int>{2020, 1999, 1962}));
}

void TestItemQuery::parsesFiltersFromEmptyPayload()
{
    const JellyfinClient::LibraryFilters filters = JellyfinClient::parseLibraryFilters(QJsonObject());
    QVERIFY(filters.genres.isEmpty());
    QVERIFY(filters.officialRatings.isEmpty());
    QVERIFY(filters.years.isEmpty());
}

void TestItemQuery::trickplayUsesAuthorizationHeader()
{
    NetworkQueue queue;
    JellyfinClient client(&queue);
    UserSession session;
    session.serverUrl = QStringLiteral("https://example.test");
    session.userId = QStringLiteral("uid-1");
    session.token = QStringLiteral("secret-tok");
    client.setSession(session);

    const QUrl url = client.trickplayTileUrl(QStringLiteral("item-42"), 320, 2);
    QCOMPARE(url.toString(), QStringLiteral("https://example.test/Videos/item-42/Trickplay/320/2.jpg"));
    QVERIFY(!url.hasQuery());

    const QNetworkRequest req = client.authorizedRequest(url);
    QCOMPARE(req.url(), url);
    const QByteArray auth = req.rawHeader("Authorization");
    QVERIFY(auth.startsWith("MediaBrowser "));
    QVERIFY(auth.contains("Token=\"secret-tok\""));
}

QTEST_MAIN(TestItemQuery)
#include "tst_itemquery.moc"
