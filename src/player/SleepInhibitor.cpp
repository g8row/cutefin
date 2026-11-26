#include "player/SleepInhibitor.h"

#include <QLoggingCategory>

#if defined(Q_OS_MACOS)
#include <IOKit/pwr_mgt/IOPMLib.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(CUTEFIN_HAVE_DBUS)
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

namespace {
Q_LOGGING_CATEGORY(lcSleep, "cutefin.sleep")

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN) && defined(CUTEFIN_HAVE_DBUS)
const QString kScreenSaverService = QStringLiteral("org.freedesktop.ScreenSaver");
const QString kScreenSaverPath = QStringLiteral("/org/freedesktop/ScreenSaver");
const QString kScreenSaverInterface = QStringLiteral("org.freedesktop.ScreenSaver");
// Names the in-flight Inhibit call's watcher (a child of the inhibitor), so
// a second request while one is pending doesn't issue another.
const QString kInhibitWatcherName = QStringLiteral("cutefin-screensaver-inhibit");

QDBusMessage screenSaverCall(const QString &method)
{
    return QDBusMessage::createMethodCall(kScreenSaverService, kScreenSaverPath, kScreenSaverInterface, method);
}

void sendUnInhibit(quint32 cookie)
{
    QDBusMessage message = screenSaverCall(QStringLiteral("UnInhibit"));
    message << cookie;
    // Fire and forget: nothing useful to do with the (empty) reply.
    QDBusConnection::sessionBus().send(message);
}
#endif
} // namespace

SleepInhibitor::SleepInhibitor(QObject *parent)
    : QObject(parent)
{
}

SleepInhibitor::~SleepInhibitor()
{
    setInhibited(false);
}

void SleepInhibitor::setInhibited(bool inhibited, const QString &reason)
{
    if (inhibited == m_inhibited)
        return;

#if defined(Q_OS_MACOS)
    if (inhibited) {
        const QByteArray reasonUtf8 = reason.toUtf8();
        CFStringRef cfReason = CFStringCreateWithCString(kCFAllocatorDefault, reasonUtf8.constData(),
                                                         kCFStringEncodingUTF8);
        IOPMAssertionID assertionId = kIOPMNullAssertionID;
        const IOReturn rc = IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
                                                        cfReason, &assertionId);
        if (cfReason)
            CFRelease(cfReason);
        if (rc != kIOReturnSuccess) {
            qCWarning(lcSleep, "IOPMAssertionCreateWithName failed: 0x%x", static_cast<unsigned>(rc));
            return;
        }
        m_cookie = static_cast<quint32>(assertionId);
    } else {
        if (m_cookie != 0)
            IOPMAssertionRelease(static_cast<IOPMAssertionID>(m_cookie));
        m_cookie = 0;
    }
    m_inhibited = inhibited;

#elif defined(Q_OS_WIN)
    Q_UNUSED(reason);
    // ES_CONTINUOUS keeps the state until the next call from this thread;
    // setInhibited() is only ever called from the GUI thread.
    if (inhibited)
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    else
        SetThreadExecutionState(ES_CONTINUOUS);
    m_inhibited = inhibited;

#elif defined(CUTEFIN_HAVE_DBUS)
    // Entirely asynchronous: QDBusInterface would introspect and call
    // synchronously on the GUI thread, stalling playback start/pause for as
    // long as a slow (or absent) session bus takes to answer. The logical
    // state is updated immediately; a missing service just turns the reply
    // into a logged no-op.
    m_inhibited = inhibited;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_cookie = 0;
        return;
    }

    if (inhibited) {
        if (findChild<QDBusPendingCallWatcher *>(kInhibitWatcherName, Qt::FindDirectChildrenOnly))
            return; // an Inhibit is already in flight; its reply sees m_inhibited
        const QString appName =
            QCoreApplication::applicationName().isEmpty() ? QStringLiteral("cutefin") : QCoreApplication::applicationName();
        QDBusMessage message = screenSaverCall(QStringLiteral("Inhibit"));
        message << appName << reason;

        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
        watcher->setObjectName(kInhibitWatcherName);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
            call->deleteLater();
            call->setObjectName(QString()); // no longer counts as pending
            const QDBusPendingReply<quint32> reply = *call;
            if (reply.isError()) {
                // No screensaver service (minimal WM, headless): no-op.
                qCDebug(lcSleep, "ScreenSaver.Inhibit failed: %ls", qUtf16Printable(reply.error().message()));
                return;
            }
            const quint32 cookie = reply.value();
            if (m_inhibited) {
                m_cookie = cookie;
            } else if (cookie != 0) {
                // Released while the call was in flight: undo it now.
                sendUnInhibit(cookie);
            }
        });
    } else if (m_cookie != 0) {
        sendUnInhibit(m_cookie);
        m_cookie = 0;
    }

#else
    Q_UNUSED(reason);
    m_inhibited = inhibited;
#endif
}
