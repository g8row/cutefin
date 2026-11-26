import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

ColumnLayout {
    id: root

    spacing: 12

    // -----------------------------------------------------------------------
    // Skip Segment Floating Pill (Intro / Outro / Recap / Commercial)
    // -----------------------------------------------------------------------
    Rectangle {
        id: skipPill
        Layout.alignment: Qt.AlignRight
        visible: opacity > 0.01
        opacity: player.skipActive ? 1.0 : 0.0
        implicitWidth: skipRow.implicitWidth + 30
        implicitHeight: 42
        radius: 21
        color: skipArea.pressed
            ? Theme.accentHover
            : (skipArea.containsMouse ? Theme.accent : Qt.rgba(0.08, 0.09, 0.14, 0.92))
        border.width: 1.5
        border.color: skipArea.containsMouse ? Theme.accentHover : Theme.accent

        Behavior on opacity {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animNormal }
        }
        Behavior on color {
            enabled: !app.reduceMotion
            ColorAnimation { duration: Theme.animFast }
        }

        RowLayout {
            id: skipRow
            anchors.centerIn: parent
            spacing: 8

            Image {
                width: 16
                height: 16
                sourceSize: Qt.size(32, 32)
                source: "data:image/svg+xml," + encodeURIComponent(
                    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#FFFFFF' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'>"
                    + "<polygon points='5 4 15 12 5 20 5 4'/><line x1='19' y1='5' x2='19' y2='19'/></svg>"
                )
            }

            Text {
                text: player.skipLabel.length > 0 ? player.skipLabel : "Skip"
                color: "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
            }
        }

        MouseArea {
            id: skipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: player.triggerSkip()
        }
    }

    // -----------------------------------------------------------------------
    // Next Episode Countdown Card
    // -----------------------------------------------------------------------
    Rectangle {
        id: upNextCard
        Layout.alignment: Qt.AlignRight
        visible: opacity > 0.01
        opacity: player.nextEpisodeVisible ? 1.0 : 0.0
        implicitWidth: 364
        implicitHeight: upNextLayout.implicitHeight + 24
        radius: Theme.radiusLg
        color: Qt.rgba(0.07, 0.08, 0.12, 0.94)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.16)

        Behavior on opacity {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animNormal }
        }

        // Prevent card background clicks from pausing video
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
        }

        ColumnLayout {
            id: upNextLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                // 16:9 Episode Thumbnail
                Rectangle {
                    Layout.preferredWidth: 124
                    Layout.preferredHeight: 70
                    radius: Theme.radiusSm
                    color: "#141824"
                    clip: true

                    Image {
                        id: upNextThumb
                        anchors.fill: parent
                        source: player.nextEpisodeThumbUrl
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: false
                        retainWhileLoading: true
                        smooth: true
                    }

                    // Subtle play badge over thumbnail
                    Rectangle {
                        anchors.centerIn: parent
                        width: 28
                        height: 28
                        radius: 14
                        color: Qt.rgba(0, 0, 0, 0.60)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.25)

                        Image {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 1
                            width: 12
                            height: 12
                            sourceSize: Qt.size(24, 24)
                            source: "data:image/svg+xml," + encodeURIComponent(
                                "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='#FFFFFF'>"
                                + "<polygon points='6 4 20 12 6 20 6 4'/></svg>"
                            )
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Text {
                        text: (app.autoPlayNextEpisode && player.nextEpisodeCountdown > 0)
                            ? ("UP NEXT IN " + player.nextEpisodeCountdown + "s")
                            : "UP NEXT"
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 0.7
                    }

                    Text {
                        text: player.nextEpisodeTitle.length > 0 ? player.nextEpisodeTitle : "Next Episode"
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        visible: player.nextEpisodeSubtitle.length > 0
                        text: player.nextEpisodeSubtitle
                        color: Qt.rgba(1, 1, 1, 0.68)
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    id: playNowBtn
                    Layout.fillWidth: true
                    implicitHeight: 36
                    radius: Theme.radiusMd
                    color: playNowArea.pressed
                        ? Theme.accentHover
                        : (playNowArea.containsMouse ? Theme.accentHover : Theme.accent)

                    Behavior on color {
                        enabled: !app.reduceMotion
                        ColorAnimation { duration: Theme.animFast }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: (app.autoPlayNextEpisode && player.nextEpisodeCountdown > 0)
                            ? ("Play Now (" + player.nextEpisodeCountdown + ")")
                            : "Play Now"
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        id: playNowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: player.playNextEpisode()
                    }
                }

                Rectangle {
                    id: cancelBtn
                    implicitWidth: 92
                    implicitHeight: 36
                    radius: Theme.radiusMd
                    color: cancelArea.pressed
                        ? Qt.rgba(1, 1, 1, 0.18)
                        : (cancelArea.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06))
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.16)

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: "#E8ECF5"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.Medium
                    }

                    MouseArea {
                        id: cancelArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: player.cancelNextEpisode()
                    }
                }
            }
        }
    }
}
