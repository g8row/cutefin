import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Item {
    id: root

    property alias text: input.text
    property string placeholderText: ""
    property string iconName: ""
    property bool password: false

    signal accepted()

    function focusInput() {
        input.forceActiveFocus();
    }

    implicitWidth: 280
    implicitHeight: 42

    // Subtle focus outer ring (shader-free)
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: Theme.radiusMd + 3
        color: Theme.accentMuted
        visible: input.activeFocus
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: Theme.bgInput
        border.width: input.activeFocus ? 1.5 : 1
        border.color: input.activeFocus ? Theme.accent : (hoverHandler.hovered ? Theme.borderStrong : Theme.border)

        Behavior on border.color {
            ColorAnimation { duration: Theme.animFast }
        }

        HoverHandler {
            id: hoverHandler
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spaceMd - 2
            anchors.rightMargin: Theme.spaceSm + 2
            spacing: Theme.spaceSm

            AppIcon {
                Layout.alignment: Qt.AlignVCenter
                visible: root.iconName.length > 0
                name: root.iconName
                size: 16
                color: input.activeFocus ? Theme.accent : Theme.textMuted
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    text: root.placeholderText
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                    visible: input.text.length === 0 && !input.preeditText
                }

                TextInput {
                    id: input
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    color: Theme.textPrimary
                    selectionColor: Theme.accent
                    selectedTextColor: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    echoMode: root.password ? TextInput.Password : TextInput.Normal
                    passwordCharacter: "•"
                    clip: true
                    selectByMouse: true
                    activeFocusOnTab: true
                    onAccepted: root.accepted()
                }
            }

            // Clear button
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                radius: 12
                visible: input.text.length > 0
                color: clearArea.containsMouse ? Theme.bgElevated : "transparent"

                AppIcon {
                    anchors.centerIn: parent
                    name: "close"
                    size: 13
                    color: clearArea.containsMouse ? Theme.textPrimary : Theme.textMuted
                }

                MouseArea {
                    id: clearArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        input.text = "";
                        input.forceActiveFocus();
                    }
                }
            }
        }
    }
}
