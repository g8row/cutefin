import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import Cutefin

Item {
    id: root

    property string name: ""
    property int size: 18
    property color color: Theme.textPrimary

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    IconImage {
        anchors.fill: parent
        source: root.name ? ("qrc:/qt/qml/Cutefin/qml/icons/" + root.name + ".svg") : ""
        color: root.color
        sourceSize: Qt.size(Math.max(1, root.size) * 2, Math.max(1, root.size) * 2)
        mipmap: false
    }
}
