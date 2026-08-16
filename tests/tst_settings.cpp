#include "core/Settings.h"

#include <QTemporaryDir>
#include <QTest>

class tst_Settings : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void deviceIdIsStableAcrossInstances();
    void recentServersRoundTrip();
    void rememberedSessionPersistsAcrossInstances();
    void unrememberedSessionDoesNotPersist();
    void clearSessionWipesPersistedTokenAndUser();

private:
    QTemporaryDir *m_dir = nullptr;
    QString iniPath() const { return m_dir->filePath(QStringLiteral("settings.ini")); }
};

void tst_Settings::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
}

void tst_Settings::cleanup()
{
    delete m_dir;
    m_dir = nullptr;
}

void tst_Settings::deviceIdIsStableAcrossInstances()
{
    Settings first(iniPath());
    const QString id = first.deviceId();
    QVERIFY(!id.isEmpty());

    Settings second(iniPath());
    QCOMPARE(second.deviceId(), id);
}

void tst_Settings::recentServersRoundTrip()
{
    Settings settings(iniPath());
    settings.addRecentServer(QStringLiteral("https://a.example.com"));
    settings.addRecentServer(QStringLiteral("https://b.example.com"));
    // Re-adding an existing entry should move it to the front, not duplicate it.
    settings.addRecentServer(QStringLiteral("https://a.example.com"));

    const QStringList recents = settings.recentServers();
    QCOMPARE(recents.size(), 2);
    QCOMPARE(recents.first(), QStringLiteral("https://a.example.com"));
    QCOMPARE(recents.last(), QStringLiteral("https://b.example.com"));
}

void tst_Settings::rememberedSessionPersistsAcrossInstances()
{
    Settings::Session session;
    session.serverUrl = QStringLiteral("https://jelly.example.com");
    session.userId = QStringLiteral("user-1");
    session.userName = QStringLiteral("alice");
    session.token = QStringLiteral("secret-token");

    {
        Settings settings(iniPath());
        session.deviceId = settings.deviceId();
        settings.saveSession(session, /*rememberMe=*/true);
        QCOMPARE(settings.currentSession().token, QStringLiteral("secret-token"));
    }

    Settings reopened(iniPath());
    QVERIFY(reopened.hasPersistedSession());
    const Settings::Session loaded = reopened.loadPersistedSession();
    QCOMPARE(loaded.serverUrl, session.serverUrl);
    QCOMPARE(loaded.userId, session.userId);
    QCOMPARE(loaded.userName, session.userName);
    QCOMPARE(loaded.token, session.token);
}

void tst_Settings::unrememberedSessionDoesNotPersist()
{
    Settings::Session session;
    session.serverUrl = QStringLiteral("https://jelly.example.com");
    session.userId = QStringLiteral("user-2");
    session.userName = QStringLiteral("bob");
    session.token = QStringLiteral("in-memory-only-token");

    {
        Settings settings(iniPath());
        settings.saveSession(session, /*rememberMe=*/false);
        // The token is available for the current process...
        QCOMPARE(settings.currentSession().token, QStringLiteral("in-memory-only-token"));
    }

    // ...but a fresh instance (simulating a restart) never sees a token, so
    // it must not report a usable persisted session.
    Settings reopened(iniPath());
    QVERIFY(!reopened.hasPersistedSession());
    QVERIFY(reopened.loadPersistedSession().token.isEmpty());
}

void tst_Settings::clearSessionWipesPersistedTokenAndUser()
{
    Settings::Session session;
    session.serverUrl = QStringLiteral("https://jelly.example.com");
    session.userId = QStringLiteral("user-3");
    session.userName = QStringLiteral("carol");
    session.token = QStringLiteral("some-token");

    Settings settings(iniPath());
    settings.saveSession(session, true);
    QVERIFY(settings.hasPersistedSession());

    settings.clearSession();
    QVERIFY(!settings.hasPersistedSession());
    QVERIFY(settings.currentSession().token.isEmpty());
    QVERIFY(settings.currentSession().userId.isEmpty());

    Settings reopened(iniPath());
    QVERIFY(!reopened.hasPersistedSession());
    QVERIFY(reopened.loadPersistedSession().token.isEmpty());
}

QTEST_APPLESS_MAIN(tst_Settings)
#include "tst_settings.moc"
