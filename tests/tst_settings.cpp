#include "core/Settings.h"
#include "core/TrackSelection.h"

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
    void autoSkipSegmentsDefaultsOffAndRoundTrips();
    void autoPlayNextEpisodeDefaultsOnAndRoundTrips();

    // Phase 3b (FEATURES.md §C2): subtitle/audio presentation + remembered
    // per-series track choices.
    void subtitlePresentationDefaultsMatchMpvAndRoundTrip();
    void audioPresentationDefaultsAndRoundTrip();
    void preferredLanguagesDefaultEmptyAndRoundTrip();
    void rememberedTrackChoiceHasNoChoiceUntilSet();
    void rememberedTrackChoiceRoundTripsAcrossInstances();
    void rememberedTrackChoicesAreIndependentPerSeriesAndKind();
    void rememberedTrackChoiceCanBeCleared();

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

// Phase 3b (FEATURES.md §C2/§C5): auto-skip defaults off (an unannounced
// seek is more surprising than a button the user can ignore), auto-play-next
// defaults on (every reference client in FEATURES.md §A does, and the
// countdown card always gives a Cancel first, so it is never silent) --
// see the accessor doc comments in Settings.h for the full reasoning.
void tst_Settings::autoSkipSegmentsDefaultsOffAndRoundTrips()
{
    Settings settings(iniPath());
    QVERIFY(!settings.autoSkipSegments());

    settings.setAutoSkipSegments(true);
    QVERIFY(settings.autoSkipSegments());

    Settings reopened(iniPath());
    QVERIFY(reopened.autoSkipSegments());
}

void tst_Settings::autoPlayNextEpisodeDefaultsOnAndRoundTrips()
{
    Settings settings(iniPath());
    QVERIFY(settings.autoPlayNextEpisode());

    settings.setAutoPlayNextEpisode(false);
    QVERIFY(!settings.autoPlayNextEpisode());

    Settings reopened(iniPath());
    QVERIFY(!reopened.autoPlayNextEpisode());
}

// mpv's own defaults (55pt, 1.65 outline, sub-pos 100, opaque white text) --
// a user who never touches these must see exactly what libmpv would render
// standalone (see the reasoning in Settings.cpp's kDefault* constants).
void tst_Settings::subtitlePresentationDefaultsMatchMpvAndRoundTrip()
{
    Settings settings(iniPath());
    QCOMPARE(settings.subtitleFontSize(), 55);
    QCOMPARE(settings.subtitleOutlineSize(), 1.65);
    QCOMPARE(settings.subtitleShadowOffset(), 0.0);
    QCOMPARE(settings.subtitlePositionPercent(), 100);
    QCOMPARE(settings.subtitleDelaySeconds(), 0.0);
    QVERIFY(!settings.subtitleAssOverride());
    QVERIFY(!settings.subtitleBackgroundBoxEnabled());
    QCOMPARE(settings.subtitleColor(), QStringLiteral("#FFFFFFFF"));
    QVERIFY(settings.subtitleFontFamily().isEmpty());

    settings.setSubtitleFontSize(72);
    settings.setSubtitleFontFamily(QStringLiteral("Helvetica"));
    settings.setSubtitleColor(QStringLiteral("#FF00FF00"));
    settings.setSubtitleOutlineColor(QStringLiteral("#FFFF0000"));
    settings.setSubtitleOutlineSize(3.5);
    settings.setSubtitleShadowOffset(1.2);
    settings.setSubtitleBackgroundBoxEnabled(true);
    settings.setSubtitleBackgroundColor(QStringLiteral("#C0000000"));
    settings.setSubtitlePositionPercent(80);
    settings.setSubtitleDelaySeconds(-0.3);
    settings.setSubtitleAssOverride(true);

    Settings reopened(iniPath());
    QCOMPARE(reopened.subtitleFontSize(), 72);
    QCOMPARE(reopened.subtitleFontFamily(), QStringLiteral("Helvetica"));
    QCOMPARE(reopened.subtitleColor(), QStringLiteral("#FF00FF00"));
    QCOMPARE(reopened.subtitleOutlineColor(), QStringLiteral("#FFFF0000"));
    QCOMPARE(reopened.subtitleOutlineSize(), 3.5);
    QCOMPARE(reopened.subtitleShadowOffset(), 1.2);
    QVERIFY(reopened.subtitleBackgroundBoxEnabled());
    QCOMPARE(reopened.subtitleBackgroundColor(), QStringLiteral("#C0000000"));
    QCOMPARE(reopened.subtitlePositionPercent(), 80);
    QCOMPARE(reopened.subtitleDelaySeconds(), -0.3);
    QVERIFY(reopened.subtitleAssOverride());
}

void tst_Settings::audioPresentationDefaultsAndRoundTrip()
{
    Settings settings(iniPath());
    QCOMPARE(settings.audioDelaySeconds(), 0.0);
    QCOMPARE(settings.audioDevice(), QStringLiteral("auto"));
    QVERIFY(!settings.audioPassthroughEnabled());
    QVERIFY(!settings.audioNormalizationEnabled());
    QCOMPARE(settings.audioChannelLayout(), QStringLiteral("auto"));

    settings.setAudioDelaySeconds(0.5);
    settings.setAudioDevice(QStringLiteral("coreaudio/abc123"));
    settings.setAudioPassthroughEnabled(true);
    settings.setAudioNormalizationEnabled(true);
    settings.setAudioChannelLayout(QStringLiteral("stereo"));

    Settings reopened(iniPath());
    QCOMPARE(reopened.audioDelaySeconds(), 0.5);
    QCOMPARE(reopened.audioDevice(), QStringLiteral("coreaudio/abc123"));
    QVERIFY(reopened.audioPassthroughEnabled());
    QVERIFY(reopened.audioNormalizationEnabled());
    QCOMPARE(reopened.audioChannelLayout(), QStringLiteral("stereo"));
}

void tst_Settings::preferredLanguagesDefaultEmptyAndRoundTrip()
{
    Settings settings(iniPath());
    QVERIFY(settings.preferredAudioLanguage().isEmpty());
    QVERIFY(settings.preferredSubtitleLanguage().isEmpty());

    settings.setPreferredAudioLanguage(QStringLiteral("eng"));
    settings.setPreferredSubtitleLanguage(QStringLiteral("jpn"));

    Settings reopened(iniPath());
    QCOMPARE(reopened.preferredAudioLanguage(), QStringLiteral("eng"));
    QCOMPARE(reopened.preferredSubtitleLanguage(), QStringLiteral("jpn"));
}

void tst_Settings::rememberedTrackChoiceHasNoChoiceUntilSet()
{
    Settings settings(iniPath());
    QVERIFY(!settings.rememberedAudioChoice(QStringLiteral("series-1")).hasChoice);
    QVERIFY(!settings.rememberedSubtitleChoice(QStringLiteral("series-1")).hasChoice);
    QVERIFY(!settings.rememberedSecondarySubtitleChoice(QStringLiteral("series-1")).hasChoice);
}

void tst_Settings::rememberedTrackChoiceRoundTripsAcrossInstances()
{
    TrackSelection::RememberedChoice choice;
    choice.language = QStringLiteral("eng");
    choice.codec = QStringLiteral("eac3");
    choice.forced = false;
    choice.hearingImpaired = true;
    choice.hasChoice = true;

    {
        Settings settings(iniPath());
        settings.setRememberedAudioChoice(QStringLiteral("series-1"), choice);
    }

    Settings reopened(iniPath());
    const TrackSelection::RememberedChoice loaded = reopened.rememberedAudioChoice(QStringLiteral("series-1"));
    QVERIFY(loaded.hasChoice);
    QCOMPARE(loaded.language, QStringLiteral("eng"));
    QCOMPARE(loaded.codec, QStringLiteral("eac3"));
    QVERIFY(!loaded.forced);
    QVERIFY(loaded.hearingImpaired);
}

void tst_Settings::rememberedTrackChoicesAreIndependentPerSeriesAndKind()
{
    Settings settings(iniPath());

    TrackSelection::RememberedChoice seriesOneAudio;
    seriesOneAudio.language = QStringLiteral("eng");
    seriesOneAudio.hasChoice = true;
    settings.setRememberedAudioChoice(QStringLiteral("series-1"), seriesOneAudio);

    TrackSelection::RememberedChoice seriesTwoAudio;
    seriesTwoAudio.language = QStringLiteral("jpn");
    seriesTwoAudio.hasChoice = true;
    settings.setRememberedAudioChoice(QStringLiteral("series-2"), seriesTwoAudio);

    TrackSelection::RememberedChoice seriesOneSubtitle;
    seriesOneSubtitle.language = QStringLiteral("spa");
    seriesOneSubtitle.hasChoice = true;
    settings.setRememberedSubtitleChoice(QStringLiteral("series-1"), seriesOneSubtitle);

    // A different series must not see series-1's choice (no cross-show
    // leakage), and audio/subtitle/secondary are independent slots even for
    // the same series.
    QCOMPARE(settings.rememberedAudioChoice(QStringLiteral("series-1")).language, QStringLiteral("eng"));
    QCOMPARE(settings.rememberedAudioChoice(QStringLiteral("series-2")).language, QStringLiteral("jpn"));
    QCOMPARE(settings.rememberedSubtitleChoice(QStringLiteral("series-1")).language, QStringLiteral("spa"));
    QVERIFY(!settings.rememberedSubtitleChoice(QStringLiteral("series-2")).hasChoice);
    QVERIFY(!settings.rememberedSecondarySubtitleChoice(QStringLiteral("series-1")).hasChoice);
}

void tst_Settings::rememberedTrackChoiceCanBeCleared()
{
    Settings settings(iniPath());

    TrackSelection::RememberedChoice choice;
    choice.language = QStringLiteral("eng");
    choice.hasChoice = true;
    settings.setRememberedSubtitleChoice(QStringLiteral("series-1"), choice);
    QVERIFY(settings.rememberedSubtitleChoice(QStringLiteral("series-1")).hasChoice);

    settings.setRememberedSubtitleChoice(QStringLiteral("series-1"), TrackSelection::RememberedChoice());
    QVERIFY(!settings.rememberedSubtitleChoice(QStringLiteral("series-1")).hasChoice);
}

QTEST_APPLESS_MAIN(tst_Settings)
#include "tst_settings.moc"
