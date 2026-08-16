#include "LoginPage.h"

#include "api/JellyfinClient.h"
#include "core/Settings.h"

#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kColumnMaxWidth = 380;
}

LoginPage::LoginPage(JellyfinClient *client, Settings *settings, QWidget *parent)
    : QWidget(parent)
    , m_client(client)
    , m_settings(settings)
{
    QWidget *column = new QWidget(this);
    column->setMaximumWidth(kColumnMaxWidth);

    m_headerLabel = new QLabel(column);
    QFont headerFont = m_headerLabel->font();
    headerFont.setPointSize(headerFont.pointSize() + 8);
    headerFont.setBold(true);
    m_headerLabel->setFont(headerFont);
    m_headerLabel->setWordWrap(true);

    m_userEdit = new QLineEdit(column);
    m_userEdit->setPlaceholderText(tr("Username"));
    m_userEdit->setStyleSheet(QStringLiteral("QLineEdit { padding: 6px 8px; border-radius: 6px; }"));

    m_passwordEdit = new QLineEdit(column);
    m_passwordEdit->setPlaceholderText(tr("Password"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setStyleSheet(QStringLiteral("QLineEdit { padding: 6px 8px; border-radius: 6px; }"));

    m_rememberCheck = new QCheckBox(tr("Remember me"), column);
    m_rememberCheck->setChecked(true);

    m_signInButton = new QPushButton(tr("Sign In"), column);
    m_signInButton->setDefault(true);
    m_signInButton->setStyleSheet(QStringLiteral("QPushButton { padding: 6px 16px; border-radius: 6px; }"));

    m_statusLabel = new QLabel(column);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setForegroundRole(QPalette::WindowText);

    m_changeServerButton = new QPushButton(tr("Change server"), column);
    m_changeServerButton->setFlat(true);

    QVBoxLayout *columnLayout = new QVBoxLayout(column);
    columnLayout->addWidget(m_headerLabel);
    columnLayout->addSpacing(16);
    columnLayout->addWidget(m_userEdit);
    columnLayout->addWidget(m_passwordEdit);
    columnLayout->addWidget(m_rememberCheck);
    columnLayout->addWidget(m_signInButton);
    columnLayout->addWidget(m_statusLabel);
    columnLayout->addSpacing(12);
    columnLayout->addWidget(m_changeServerButton);

    QHBoxLayout *centerRow = new QHBoxLayout();
    centerRow->addStretch();
    centerRow->addWidget(column);
    centerRow->addStretch();

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->addStretch();
    outer->addLayout(centerRow);
    outer->addStretch();

    setTabOrder(m_userEdit, m_passwordEdit);
    setTabOrder(m_passwordEdit, m_rememberCheck);
    setTabOrder(m_rememberCheck, m_signInButton);
    setTabOrder(m_signInButton, m_changeServerButton);

    connect(m_userEdit, &QLineEdit::returnPressed, this, &LoginPage::submit);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginPage::submit);
    connect(m_signInButton, &QPushButton::clicked, this, &LoginPage::submit);
    connect(m_changeServerButton, &QPushButton::clicked, this, &LoginPage::changeServerRequested);
}

void LoginPage::setServer(const ServerInfo &info)
{
    m_server = info;
    m_headerLabel->setText(tr("Sign in to %1").arg(info.name));
}

void LoginPage::setUserName(const QString &userName)
{
    m_userEdit->setText(userName);
}

void LoginPage::takeFocus()
{
    if (m_userEdit->text().isEmpty())
        m_userEdit->setFocus();
    else
        m_passwordEdit->setFocus();
}

void LoginPage::submit()
{
    if (m_busy)
        return;

    const QString userName = m_userEdit->text().trimmed();
    if (userName.isEmpty()) {
        showError(tr("Enter a username."));
        m_userEdit->setFocus();
        return;
    }

    const QString password = m_passwordEdit->text();

    setBusy(true);
    m_statusLabel->setForegroundRole(QPalette::WindowText);
    m_statusLabel->setText(tr("Signing in…"));

    m_client->login(userName, password, this,
                     [this](bool ok, const UserSession &session, const QString &error) {
                         setBusy(false);
                         if (!ok) {
                             showError(error);
                             m_passwordEdit->clear();
                             m_passwordEdit->setFocus();
                             return;
                         }

                         Settings::Session settingsSession;
                         settingsSession.serverUrl = session.serverUrl;
                         settingsSession.userId = session.userId;
                         settingsSession.userName = session.userName;
                         settingsSession.token = session.token;
                         settingsSession.deviceId = session.deviceId;

                         m_settings->saveSession(settingsSession, m_rememberCheck->isChecked());

                         emit loggedIn(session);
                     });
}

void LoginPage::setBusy(bool busy)
{
    m_busy = busy;
    m_signInButton->setEnabled(!busy);
    m_userEdit->setEnabled(!busy);
    m_passwordEdit->setEnabled(!busy);
    m_changeServerButton->setEnabled(!busy);
}

void LoginPage::showError(const QString &message)
{
    // See ServerSelectPage::showError: QPalette::BrightText is near-white on
    // a light theme, so weight + a glyph carry the meaning instead of colour.
    QFont font = m_statusLabel->font();
    font.setBold(true);
    m_statusLabel->setFont(font);
    m_statusLabel->setText(QStringLiteral("⚠  ") + message);
}
