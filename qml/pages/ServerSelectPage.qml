import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root
    color: Theme.bgCanvas

    // Ambient Jellyfin-style gradient backdrop (pure QtQuick Rectangle + Gradient, zero shaders).
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, Theme.isDark ? 0.16 : 0.08) }
            GradientStop { position: 0.45; color: Theme.bgCanvas }
            GradientStop { position: 1.0; color: Qt.rgba(Theme.accentSecondary.r, Theme.accentSecondary.g, Theme.accentSecondary.b, Theme.isDark ? 0.10 : 0.05) }
        }
    }

    // Soft drop shadow (layered rectangle, dual-core friendly).
    Rectangle {
        anchors.fill: card
        anchors.topMargin: 10
        z: -1
        radius: Theme.radiusXl
        color: "#000000"
        opacity: Theme.isDark ? 0.5 : 0.12
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(460, root.width - Theme.spaceXl * 2)
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
            spacing: Theme.spaceMd

            // Branding
            RowLayout {
                spacing: Theme.spaceMd
                Rectangle {
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    radius: 13
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.accent }
                        GradientStop { position: 1.0; color: Theme.accentSecondary }
                    }
                    AppIcon {
                        anchors.centerIn: parent
                        name: "play"
                        size: 20
                        color: "#FFFFFF"
                    }
                }
                ColumnLayout {
                    spacing: 1
                    Text {
                        text: "Cutefin"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontDisplay
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "Jellyfin client"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXs
                Text {
                    text: "Connect to your server"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontH1
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: "Enter the address of your Jellyfin server to get started."
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
            }

            AppTextField {
                id: serverField
                Layout.fillWidth: true
                iconName: "server"
                placeholderText: "http://192.168.1.10:8096"
                text: app.serverUrl
                onAccepted: root.connectToServer()
            }

            AppButton {
                Layout.fillWidth: true
                text: "Connect"
                iconName: "chevron-right"
                variant: "primary"
                loading: app.authBusy
                enabled: !app.authBusy && serverField.text.trim().length > 0
                onClicked: root.connectToServer()
            }

            // Inline error banner
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: errorRow.implicitHeight + Theme.spaceMd * 2
                visible: app.authError.length > 0
                radius: Theme.radiusMd
                color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
                border.width: 1
                border.color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.45)

                RowLayout {
                    id: errorRow
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

            // Recent servers
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                visible: app.recentServers.length > 0

                Text {
                    text: "Recent servers"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.weight: Font.DemiBold
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm
                    Repeater {
                        model: app.recentServers
                        delegate: BadgePill {
                            text: modelData
                            iconName: "server"
                            clickable: true
                            tint: Theme.bgCard
                            textColor: Theme.textSecondary
                            onClicked: {
                                serverField.text = modelData
                                root.connectToServer()
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: !app.quickConnectActive
                text: "After connecting, you can also sign in with Quick Connect from your phone."
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
        }
    }

    function connectToServer() {
        const url = serverField.text.trim()
        if (url.length > 0 && !app.authBusy)
            app.probeServer(url)
    }
}
