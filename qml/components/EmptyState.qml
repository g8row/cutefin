import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Item {
    id: root

    property string iconName: "film"
    property string title: ""
    property string subtitle: ""
    property string actionText: ""

    signal actionClicked()

    implicitWidth: Math.max(320, col.implicitWidth)
    implicitHeight: col.implicitHeight + Theme.spaceXl * 2

    ColumnLayout {
        id: col
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spaceLg * 2, 420)
        spacing: Theme.spaceMd

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            radius: 32
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.borderStrong

            AppIcon {
                anchors.centerIn: parent
                name: root.iconName
                size: 28
                color: Theme.textSecondary
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            horizontalAlignment: Text.AlignHCenter
            visible: root.title.length > 0
            text: root.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontH2
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            horizontalAlignment: Text.AlignHCenter
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
            lineHeight: 1.35
        }

        AppButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spaceXs
            visible: root.actionText.length > 0
            text: root.actionText
            variant: "primary"
            onClicked: root.actionClicked()
        }
    }
}
