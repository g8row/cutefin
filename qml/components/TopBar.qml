import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root

    property string title: ""
    property bool canGoBack: false

    signal backClicked()
    signal searchClicked()

    implicitHeight: 56
    height: 56
    color: Theme.bgCanvas

    // Bottom hairline divider
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spaceLg
        anchors.rightMargin: Theme.spaceLg
        spacing: Theme.spaceMd

        // Back pill button
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            visible: root.canGoBack
            implicitWidth: backRow.implicitWidth + 20
            implicitHeight: 32
            radius: 16
            color: backMouse.containsMouse ? Theme.bgCardHover : Theme.bgElevated
            border.width: 1
            border.color: backMouse.containsMouse ? Theme.borderStrong : Theme.border

            Behavior on color {
                ColorAnimation { duration: Theme.animFast }
            }

            RowLayout {
                id: backRow
                anchors.centerIn: parent
                spacing: 6

                AppIcon {
                    name: "arrow-left"
                    size: 14
                    color: Theme.textPrimary
                }

                Label {
                    text: "Back"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                }
            }

            MouseArea {
                id: backMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.backClicked()
            }
        }

        // Page title
        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontH2
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        // Clickable search hint bar
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: Math.min(320, Math.max(180, root.width * 0.34))
            implicitHeight: 36
            radius: Theme.radiusMd
            color: searchMouse.containsMouse ? Theme.bgElevated : Theme.bgInput
            border.width: 1
            border.color: searchMouse.containsMouse ? Theme.borderStrong : Theme.border

            Behavior on color {
                ColorAnimation { duration: Theme.animFast }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: Theme.spaceSm

                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    name: "search"
                    size: 15
                    color: searchMouse.containsMouse ? Theme.textPrimary : Theme.textMuted
                }

                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: "Search movies, shows, people…"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    radius: 5
                    color: Theme.bgElevated
                    border.width: 1
                    border.color: Theme.border

                    Label {
                        anchors.centerIn: parent
                        text: "/"
                        color: Theme.textMuted
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.Medium
                    }
                }
            }

            MouseArea {
                id: searchMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.searchClicked()
            }
        }
    }
}
