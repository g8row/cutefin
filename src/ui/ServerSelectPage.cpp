#include "ServerSelectPage.h"

#include "api/JellyfinClient.h"
#include "core/Settings.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kColumnMaxWidth = 380;
}

ServerSelectPage::ServerSelectPage(JellyfinClient *client, Settings *settings, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
    , m_settings(settings)
{
    QWidget *column = new QWidget(this);
    column->setMaximumWidth(kColumnMaxWidth);

    QLabel *title = new QLabel(tr("Connect to Server"), column);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 12);
    titleFont.setBold(true);
    title->setFont(titleFont);

    m_urlEdit = new QLineEdit(column);
    m_urlEdit->setPlaceholderText(tr("https://jellyfin.example.com"));
    m_urlEdit->setStyleSheet(QStringLiteral("QLineEdit { padding: 6px 8px; border-radius: 6px; }"));

    m_connectButton = new QPushButton(tr("Connect"), column);
    m_connectButton->setDefault(true);
    m_connectButton->setStyleSheet(QStringLiteral("QPushButton { padding: 6px 16px; border-radius: 6px; }"));

    m_statusLabel = new QLabel(column);
    m_statusLabel->setWordWrap(true);

    m_recentLabel = new QLabel(tr("Recent servers"), column);

    m_recentList = new QListWidget(column);
    m_recentList->setMaximumHeight(160);

    QVBoxLayout *columnLayout = new QVBoxLayout(column);
    columnLayout->addWidget(title);
    columnLayout->addSpacing(16);
    columnLayout->addWidget(m_urlEdit);
    columnLayout->addWidget(m_connectButton);
    columnLayout->addWidget(m_statusLabel);
    columnLayout->addSpacing(12);
    columnLayout->addWidget(m_recentLabel);
    columnLayout->addWidget(m_recentList);

    QHBoxLayout *centerRow = new QHBoxLayout();
    centerRow->addStretch();
    centerRow->addWidget(column);
    centerRow->addStretch();

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->addStretch();
    outer->addLayout(centerRow);
    outer->addStretch();

    setTabOrder(m_urlEdit, m_connectButton);
    setTabOrder(m_connectButton, m_recentList);

    connect(m_urlEdit, &QLineEdit::returnPressed, this, &ServerSelectPage::connectToServer);
    connect(m_connectButton, &QPushButton::clicked, this, &ServerSelectPage::connectToServer);

    auto activateRecentItem = [this](QListWidgetItem *item) {
        if (!item)
            return;
        m_urlEdit->setText(item->text());
        connectToServer();
    };
    connect(m_recentList, &QListWidget::itemClicked, this, activateRecentItem);
    connect(m_recentList, &QListWidget::itemActivated, this, activateRecentItem);

    reloadRecentServers();
}

void ServerSelectPage::setServerUrl(const QString &url)
{
    m_urlEdit->setText(url);
}

void ServerSelectPage::takeFocus()
{
    m_urlEdit->setFocus();
    m_urlEdit->selectAll();
}

QString ServerSelectPage::normalizeServerUrl(const QString &input)
{
    QString normalized = input.trimmed();
    if (normalized.isEmpty())
        return QString();

    if (!normalized.contains(QStringLiteral("://")))
        normalized.prepend(QStringLiteral("https://"));

    while (normalized.endsWith(QLatin1Char('/')))
        normalized.chop(1);

    const QUrl url(normalized, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        return QString();

    return normalized;
}

void ServerSelectPage::connectToServer()
{
    if (m_busy)
        return;

    const QString normalized = normalizeServerUrl(m_urlEdit->text());
    if (normalized.isEmpty()) {
        showError(tr("Enter a valid server address."));
        return;
    }

    setBusy(true);
    showStatus(tr("Connecting…"));

    m_client->probeServer(normalized, this,
                           [this, normalized](bool ok, const ServerInfo &info, const QString &error) {
                               setBusy(false);
                               if (!ok) {
                                   showError(error);
                                   return;
                               }

                               // probeServer already stamps info.url with the url we
                               // passed in, but be explicit: callers rely on it being
                               // exactly the normalized form, never raw user input.
                               ServerInfo accepted = info;
                               accepted.url = normalized;

                               showStatus(tr("%1 · Jellyfin %2").arg(accepted.name, accepted.version));

                               m_settings->setServerUrl(normalized);
                               m_settings->addRecentServer(normalized);
                               m_client->setServerUrl(normalized);
                               reloadRecentServers();

                               emit serverAccepted(accepted);
                           });
}

void ServerSelectPage::setBusy(bool busy)
{
    m_busy = busy;
    m_connectButton->setEnabled(!busy);
    m_urlEdit->setEnabled(!busy);
    m_recentList->setEnabled(!busy);
}

void ServerSelectPage::showError(const QString &message)
{
    // Errors are marked by a glyph and weight rather than a colour role:
    // QPalette::BrightText is near-white under a light theme (it exists for
    // text drawn on a *highlighted* background), so using it here would make
    // the message invisible exactly when the user needs it. Not relying on
    // colour alone is also the accessibility rule in FEATURES.md §C6.
    QFont font = m_statusLabel->font();
    font.setBold(true);
    m_statusLabel->setFont(font);
    m_statusLabel->setText(QStringLiteral("⚠  ") + message);
}

void ServerSelectPage::showStatus(const QString &message)
{
    QFont font = m_statusLabel->font();
    font.setBold(false);
    m_statusLabel->setFont(font);
    m_statusLabel->setText(message);
}

void ServerSelectPage::reloadRecentServers()
{
    const QStringList servers = m_settings->recentServers();

    m_recentList->clear();
    m_recentList->addItems(servers);

    const bool hasServers = !servers.isEmpty();
    m_recentList->setVisible(hasServers);
    m_recentLabel->setVisible(hasServers);
}
