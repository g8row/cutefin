#pragma once

#include <QObject>
#include <QString>

// Cross-platform display sleep / screensaver inhibitor active during video
// playback (IOPMAssertion on macOS, SetThreadExecutionState on Windows,
// org.freedesktop.ScreenSaver D-Bus or graceful no-op on Linux).
class SleepInhibitor : public QObject
{
    Q_OBJECT
public:
    explicit SleepInhibitor(QObject *parent = nullptr);
    ~SleepInhibitor() override;

    void setInhibited(bool inhibited, const QString &reason = QStringLiteral("Playing video"));
    bool isInhibited() const { return m_inhibited; }

private:
    bool m_inhibited = false;
    quint32 m_cookie = 0;
};
