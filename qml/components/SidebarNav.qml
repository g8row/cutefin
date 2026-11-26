import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root

    property bool compact: false
    property string activeKey: "home"
    property var librariesModel
    property string userName: ""
    property string serverName: ""

    signal navSelected(string key)
    signal librarySelected(string id, string name, string collectionType)
    signal logoutRequested()

    implicitWidth: compact ? 72 : 240
    width: implicitWidth
    color: Theme.bgSurface

    Behavior on width {
        NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic }
    }

    function libraryIconName(collectionType) {
        if (collectionType === "movies")
            return "film";
        if (collectionType === "tvshows")
            return "tv";
        return "folder";
    }

    component NavItemRow: Rectangle {
        id: navItem

        property string label: ""
        property string iconName: ""
        property bool active: false

        signal triggered()

        Layout.fillWidth: true
        implicitHeight: 38
        radius: Theme.radiusMd
        color: active
            ? Theme.accentMuted
            : (itemMouse.containsMouse ? Theme.bgElevated : "transparent")

        Behavior on color {
            ColorAnimation { duration: Theme.animFast }
        }

        // Left active indicator bar
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 20
            radius: 1.5
            color: Theme.accent
            visible: navItem.active
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: root.compact ? 0 : Theme.spaceMd - 2
            anchors.rightMargin: root.compact ? 0 : Theme.spaceSm
            spacing: Theme.spaceSm + 2

            Item {
                Layout.fillWidth: root.compact
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 18
                implicitHeight: 18

                AppIcon {
                    anchors.centerIn: parent
                    name: navItem.iconName
                    size: 18
                    color: navItem.active
                        ? Theme.accent
                        : (itemMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                visible: !root.compact
                text: navItem.label
                color: navItem.active
                    ? Theme.textPrimary
                    : (itemMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: navItem.active ? Font.DemiBold : Font.Medium
                elide: Text.ElideRight
            }
        }

        ToolTip.visible: root.compact && itemMouse.containsMouse && navItem.label.length > 0
        ToolTip.delay: 350
        ToolTip.text: navItem.label

        MouseArea {
            id: itemMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: navItem.triggered()
        }
    }

    // Right hairline border
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.spaceMd
        anchors.bottomMargin: Theme.spaceMd
        anchors.leftMargin: root.compact ? Theme.spaceSm + 2 : Theme.spaceMd - 2
        anchors.rightMargin: root.compact ? Theme.spaceSm + 2 : Theme.spaceMd - 2
        spacing: Theme.spaceMd

        // Cutefin brand header
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.compact ? 0 : Theme.spaceXs
            spacing: Theme.spaceSm + 2

            Item {
                Layout.fillWidth: root.compact
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 34
                implicitHeight: 34

                Rectangle {
                    anchors.centerIn: parent
                    width: 34
                    height: 34
                    radius: Theme.radiusSm + 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.accent }
                        GradientStop { position: 1.0; color: Theme.accentSecondary }
                    }
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.20)

                    AppIcon {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 1
                        name: "play"
                        size: 15
                        color: "#FFFFFF"
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                visible: !root.compact
                text: "Cutefin"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontH2
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
        }

        // Primary navigation
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            NavItemRow {
                label: "Home"
                iconName: "home"
                active: root.activeKey === "home"
                onTriggered: root.navSelected("home")
            }

            NavItemRow {
                label: "Search"
                iconName: "search"
                active: root.activeKey === "search"
                onTriggered: root.navSelected("search")
            }

            NavItemRow {
                label: "Favorites"
                iconName: "heart"
                active: root.activeKey === "favorites"
                onTriggered: root.navSelected("favorites")
            }
        }

        // Libraries section header / divider
        Item {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceXs
            implicitHeight: root.compact ? 1 : 18

            Label {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spaceSm + 2
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.compact
                text: "LIBRARIES"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                font.weight: Font.Bold
                font.letterSpacing: 0.9
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spaceSm
                anchors.rightMargin: Theme.spaceSm
                height: 1
                color: Theme.border
                visible: root.compact
            }
        }

        // Dynamic Libraries list
        ListView {
            id: librariesList
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.librariesModel

            delegate: NavItemRow {
                readonly property var rowData: (typeof modelData !== "undefined" && modelData && modelData.id !== undefined) ? modelData : model
                readonly property string libId: (rowData && rowData.id) ? String(rowData.id) : ""
                readonly property string libName: (rowData && rowData.name) ? String(rowData.name) : ""
                readonly property string libType: (rowData && rowData.collectionType) ? String(rowData.collectionType) : ""

                width: librariesList.width
                label: libName
                iconName: root.libraryIconName(libType)
                active: libId.length > 0 && (root.activeKey === ("library:" + libId) || root.activeKey === libId)
                onTriggered: root.librarySelected(libId, libName, libType)
            }
        }

        // Bottom Settings & User/Server card
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            NavItemRow {
                label: "Settings"
                iconName: "settings"
                active: root.activeKey === "settings"
                onTriggered: root.navSelected("settings")
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: root.compact ? 44 : 52
                radius: Theme.radiusMd
                color: Theme.bgElevated
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: root.compact ? 0 : Theme.spaceSm + 2
                    anchors.rightMargin: root.compact ? 0 : Theme.spaceSm
                    spacing: Theme.spaceSm

                    // Avatar circle (when expanded)
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        visible: !root.compact
                        radius: 15
                        color: Theme.accentMuted

                        AppIcon {
                            anchors.centerIn: parent
                            name: "user"
                            size: 15
                            color: Theme.accent
                        }
                    }

                    // User + Server labels
                    Column {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        visible: !root.compact
                        spacing: 1

                        Label {
                            width: parent.width
                            text: root.userName.length > 0 ? root.userName : "Signed In"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Label {
                            width: parent.width
                            text: root.serverName.length > 0 ? root.serverName : "Jellyfin Server"
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }
                    }

                    // Logout button
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.fillWidth: root.compact
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        radius: Theme.radiusSm
                        color: logoutMouse.containsMouse ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.16) : "transparent"

                        AppIcon {
                            anchors.centerIn: parent
                            name: "logout"
                            size: 15
                            color: logoutMouse.containsMouse ? Theme.danger : Theme.textSecondary
                        }

                        ToolTip.visible: logoutMouse.containsMouse
                        ToolTip.delay: 300
                        ToolTip.text: "Sign out"

                        MouseArea {
                            id: logoutMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.logoutRequested()
                        }
                    }
                }
            }
        }
    }
}
