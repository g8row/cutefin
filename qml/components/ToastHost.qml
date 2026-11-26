import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Item {
    id: root

    property int _nextId: 1

    implicitWidth: 360
    implicitHeight: toastColumn.implicitHeight
    width: Math.min(360, parent ? parent.width - Theme.spaceLg * 2 : 360)
    z: 1000

    function show(message, kind) {
        if (!message)
            return;
        const normalizedKind = (kind === "success" || kind === "error") ? kind : "info";
        while (toastModel.count >= 4)
            toastModel.remove(0);
        toastModel.append({
            uid: _nextId++,
            message: String(message),
            kind: normalizedKind
        });
    }

    function _removeByUid(uid) {
        for (let i = 0; i < toastModel.count; ++i) {
            if (toastModel.get(i).uid === uid) {
                toastModel.remove(i);
                return;
            }
        }
    }

    ListModel {
        id: toastModel
    }

    Column {
        id: toastColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Theme.spaceSm

        move: Transition {
            NumberAnimation { properties: "y"; duration: Theme.animFast; easing.type: Easing.OutCubic }
        }

        Repeater {
            model: toastModel

            delegate: Rectangle {
                id: toastCard
                required property int uid
                required property string message
                required property string kind

                readonly property color kindColor: kind === "success"
                    ? Theme.success
                    : (kind === "error" ? Theme.danger : Theme.accentSecondary)

                width: toastColumn.width
                implicitHeight: Math.max(46, toastRow.implicitHeight + Theme.spaceSm * 2)
                height: implicitHeight
                radius: Theme.radiusMd
                color: Theme.bgElevated
                border.width: 1
                border.color: Theme.borderStrong
                clip: true

                opacity: 0.0
                Component.onCompleted: opacity = 1.0

                Behavior on opacity {
                    NumberAnimation { duration: Theme.animFast }
                }

                Timer {
                    interval: 3500
                    running: true
                    repeat: false
                    onTriggered: root._removeByUid(toastCard.uid)
                }

                // Left accent stripe
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    color: toastCard.kindColor
                }

                RowLayout {
                    id: toastRow
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceMd
                    anchors.rightMargin: Theme.spaceSm
                    anchors.topMargin: Theme.spaceSm
                    anchors.bottomMargin: Theme.spaceSm
                    spacing: Theme.spaceSm

                    AppIcon {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        size: 16
                        name: toastCard.kind === "success" ? "check" : "info"
                        color: toastCard.kindColor
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: toastCard.message
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.Medium
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        radius: 12
                        color: dismissArea.containsMouse ? Theme.bgCardHover : "transparent"

                        AppIcon {
                            anchors.centerIn: parent
                            name: "close"
                            size: 13
                            color: dismissArea.containsMouse ? Theme.textPrimary : Theme.textMuted
                        }

                        MouseArea {
                            id: dismissArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root._removeByUid(toastCard.uid)
                        }
                    }
                }
            }
        }
    }
}
