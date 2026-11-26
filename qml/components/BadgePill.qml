import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root

    property string text: ""
    property string iconName: ""
    property color tint: Theme.bgElevated
    property color textColor: Theme.textSecondary
    property bool clickable: false

    signal clicked()

    implicitWidth: pillRow.implicitWidth + (iconName ? 18 : 20)
    implicitHeight: 26
    radius: height / 2
    color: (clickable && pillArea.containsMouse) ? Qt.lighter(tint, Theme.isDark ? 1.18 : 0.94) : tint
    border.width: 1
    border.color: (clickable && pillArea.containsMouse) ? Theme.borderStrong : Theme.border

    Behavior on color {
        ColorAnimation { duration: Theme.animFast }
    }

    RowLayout {
        id: pillRow
        anchors.centerIn: parent
        spacing: 5

        AppIcon {
            Layout.alignment: Qt.AlignVCenter
            visible: root.iconName.length > 0
            name: root.iconName
            size: 12
            color: root.textColor
        }

        Label {
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: 220
            visible: root.text.length > 0
            text: root.text
            color: root.textColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: pillArea
        anchors.fill: parent
        enabled: root.clickable
        hoverEnabled: root.clickable
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
