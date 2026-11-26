import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root
    color: Theme.bgCanvas

    // Ambient gradient backdrop (pure QtQuick Rectangle + Gradient, zero shaders).
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, Theme.isDark ? 0.16 : 0.08) }
            GradientStop { position: 0.45; color: Theme.bgCanvas }
            GradientStop { position: 1.0; color: Qt.rgba(Theme.accentSecondary.r, Theme.accentSecondary.g, Theme.accentSecondary.b, Theme.isDark ? 0.10 : 0.05) }
        }
    }

    Rectangle {
        anchors.fill: card
        anchors.topMargin: 10
        z: -1
        radius: Theme.radiusXl
        color: "#000000"
        opacity: Theme.isDark ? 0.5 : 0.12
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: card.height + Theme.spaceXl * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Rectangle {
            id: card
            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.spaceXl
            width: Math.min(880, root.width - Theme.spaceXl * 2)
            height: cardContent.implicitHeight + Theme.spaceXl * 2
            radius: Theme.radiusXl
            color: Theme.bgSurface
            border.width: 1
            border.color: Theme.borderStrong

            ColumnLayout {
                id: cardContent
                x: Theme.spaceXl
                y: Theme.spaceXl
                width: parent.width - Theme.spaceXl * 2
                spacing: Theme.spaceLg

                // Server header
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        radius: 12
                        color: Theme.accentMuted
                        border.width: 1
                        border.color: Theme.accent
                        AppIcon {
                            anchors.centerIn: parent
                            name: "server"
                            size: 18
                            color: Theme.accent
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            text: app.serverName.length > 0 ? app.serverName : "Jellyfin Server"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontH1
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            text: app.serverUrl + (app.serverVersion.length > 0 ? "  ·  v" + app.serverVersion : "")
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                    AppButton {
                        text: "Change Server"
                        iconName: "logout"
                        variant: "ghost"
                        compact: true
                        onClicked: app.showServerSelect()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.border
                }

                // Two sign-in modes side by side
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXl

                    // --- Password sign-in ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceMd

                        Text {
                            text: "Sign in"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontH2
                            font.weight: Font.DemiBold
                        }

                        AppTextField {
                            id: userField
                            Layout.fillWidth: true
                            iconName: "user"
                            placeholderText: "Username"
                            onAccepted: passField.focusInput()
                        }

                        AppTextField {
                            id: passField
                            Layout.fillWidth: true
                            iconName: "check"
                            password: true
                            placeholderText: "Password"
                            onAccepted: root.doLogin()
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 20
                            RowLayout {
                                spacing: Theme.spaceSm
                                Item {
                                    id: rememberBox
                                    property bool checked: true
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 6
                                        color: rememberBox.checked ? Theme.accent : Theme.bgInput
                                        border.width: 1
                                        border.color: rememberBox.checked ? Theme.accent : Theme.borderStrong
                                        AppIcon {
                                            anchors.centerIn: parent
                                            name: "check"
                                            size: 13
                                            color: "#FFFFFF"
                                            visible: rememberBox.checked
                                        }
                                    }
                                }
                                Text {
                                    text: "Remember me"
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: rememberBox.checked = !rememberBox.checked
                            }
                        }

                        AppButton {
                            Layout.fillWidth: true
                            text: "Sign In"
                            iconName: "user"
                            variant: "primary"
                            loading: app.authBusy
                            enabled: !app.authBusy && userField.text.trim().length > 0
                            onClicked: root.doLogin()
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        color: Theme.border
                    }

                    // --- Quick Connect ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceMd

                        Text {
                            text: "Quick Connect"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontH2
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Approve the sign-in request on another device running the Jellyfin app."
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WordWrap
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 96
                            visible: !app.quickConnectActive

                            AppButton {
                                anchors.centerIn: parent
                                text: "Start Quick Connect"
                                iconName: "refresh"
                                variant: "secondary"
                                loading: app.authBusy
                                enabled: !app.authBusy
                                onClicked: app.startQuickConnect()
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm
                            visible: app.quickConnectActive

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: root.quickConnectDisplay
                                color: Theme.textPrimary
                                font.family: Theme.fontMono
                                font.pixelSize: 40
                                font.weight: Font.DemiBold
                                font.letterSpacing: 8
                            }
                            Text {
                                id: qcStatus
                                Layout.alignment: Qt.AlignHCenter
                                text: "Waiting for approval…"
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody

                                SequentialAnimation on opacity {
                                    running: app.quickConnectActive && Theme.animSlow > 0
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.35; duration: Theme.animSlow; easing.type: Easing.InOutQuad }
                                    NumberAnimation { to: 1.0; duration: Theme.animSlow; easing.type: Easing.InOutQuad }
                                }
                            }
                            AppButton {
                                Layout.alignment: Qt.AlignHCenter
                                text: "Cancel"
                                iconName: "close"
                                variant: "ghost"
                                compact: true
                                onClicked: app.cancelQuickConnect()
                            }
                        }
                    }
                }

                // Inline error banner
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: loginErrorRow.implicitHeight + Theme.spaceMd * 2
                    visible: app.authError.length > 0
                    radius: Theme.radiusMd
                    color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
                    border.width: 1
                    border.color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.45)

                    RowLayout {
                        id: loginErrorRow
                        anchors.fill: parent
                        anchors.margins: Theme.spaceMd
                        spacing: Theme.spaceSm
                        AppIcon {
                            Layout.alignment: Qt.AlignVCenter
                            name: "info"
                            size: 16
                            color: Theme.danger
                        }
                        Text {
                            Layout.fillWidth: true
                            text: app.authError
                            color: Theme.danger
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }

    readonly property string quickConnectDisplay: {
        const code = app.quickConnectCode
        if (!code)
            return ""
        if (code.length >= 6)
            return code.substring(0, 3) + " " + code.substring(3, 6) + (code.length > 6 ? " " + code.substring(6) : "")
        return code
    }

    function doLogin() {
        if (app.authBusy)
            return
        const user = userField.text.trim()
        if (user.length === 0)
            return
        app.login(user, passField.text, rememberBox.checked)
    }
}
