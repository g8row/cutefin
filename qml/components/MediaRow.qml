import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Item {
    id: root

    property string title: ""
    property var model
    property bool landscape: false
    property string libraryId: ""

    signal itemActivated(string itemId)
    signal itemPlayClicked(string itemId, bool resume)
    signal seeAllClicked(string libraryId, string title)

    readonly property real _cardWidth: landscape ? 296 : 164
    readonly property real _artHeight: landscape ? Math.round(296 * 9 / 16) : Math.round(164 * 1.5)
    readonly property real _rowHeight: _artHeight + Theme.spaceSm + 44 + 12

    implicitHeight: headerRow.implicitHeight + Theme.spaceSm + _rowHeight
    height: implicitHeight

    function scrollByCards(direction) {
        const step = (_cardWidth + Theme.spaceMd) * 3 * direction;
        const maxX = Math.max(0, listView.contentWidth - listView.width + listView.rightMargin);
        const minX = -listView.leftMargin;
        const targetX = Math.max(minX, Math.min(maxX, listView.contentX + step));
        if (Theme.animNormal <= 0) {
            listView.contentX = targetX;
        } else {
            scrollAnim.to = targetX;
            scrollAnim.restart();
        }
    }

    NumberAnimation {
        id: scrollAnim
        target: listView
        property: "contentX"
        duration: Theme.animNormal
        easing.type: Easing.OutCubic
    }

    Column {
        anchors.fill: parent
        spacing: Theme.spaceSm

        // Section Header
        RowLayout {
            id: headerRow
            width: parent.width
            height: 30

            Item { Layout.preferredWidth: Theme.spaceLg; Layout.preferredHeight: 1 }

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

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                visible: root.libraryId.length > 0
                implicitWidth: seeAllRow.implicitWidth + 16
                implicitHeight: 28
                radius: Theme.radiusSm
                color: seeAllMouse.containsMouse ? Theme.bgElevated : "transparent"

                Behavior on color {
                    ColorAnimation { duration: Theme.animFast }
                }

                RowLayout {
                    id: seeAllRow
                    anchors.centerIn: parent
                    spacing: 4

                    Label {
                        text: "See All"
                        color: seeAllMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.DemiBold
                    }

                    AppIcon {
                        name: "chevron-right"
                        size: 14
                        color: seeAllMouse.containsMouse ? Theme.textPrimary : Theme.textSecondary
                    }
                }

                MouseArea {
                    id: seeAllMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.seeAllClicked(root.libraryId, root.title)
                }
            }

            Item { Layout.preferredWidth: Theme.spaceLg; Layout.preferredHeight: 1 }
        }

        // Carousel viewport + hover chevron controls
        Item {
            id: carouselContainer
            width: parent.width
            height: root._rowHeight

            HoverHandler {
                id: carouselHover
            }

            ListView {
                id: listView
                anchors.fill: parent
                orientation: ListView.Horizontal
                spacing: Theme.spaceMd
                leftMargin: Theme.spaceLg
                rightMargin: Theme.spaceLg
                topMargin: 6
                bottomMargin: 6
                cacheBuffer: 400
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.model

                delegate: Loader {
                    id: cardLoader
                    readonly property var rowModelData: (typeof modelData !== "undefined" && modelData && modelData.id !== undefined) ? modelData : model

                    sourceComponent: root.landscape ? landscapeCardComponent : posterCardComponent

                    onLoaded: {
                        if (item)
                            item.item = Qt.binding(function() { return cardLoader.rowModelData; });
                    }
                }
            }

            Component {
                id: posterCardComponent

                PosterCard {
                    cardWidth: root._cardWidth
                    onActivated: function(itemId) { root.itemActivated(itemId); }
                    onPlayClicked: function(itemId, resume) { root.itemPlayClicked(itemId, resume); }
                }
            }

            Component {
                id: landscapeCardComponent

                LandscapeCard {
                    cardWidth: root._cardWidth
                    onActivated: function(itemId) { root.itemActivated(itemId); }
                    onPlayClicked: function(itemId, resume) { root.itemPlayClicked(itemId, resume); }
                }
            }

            // Left scroll chevron button
            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spaceSm
                y: Math.round(root._artHeight / 2 - height / 2) + 6
                width: 36
                height: 36
                radius: 18
                z: 10
                color: leftChevronMouse.containsMouse ? Theme.accent : Theme.bgOverlay
                border.width: 1
                border.color: leftChevronMouse.containsMouse ? "#FFFFFF" : Theme.borderStrong
                visible: !listView.atXBeginning && listView.contentWidth > listView.width
                opacity: (carouselHover.hovered || leftChevronMouse.containsMouse) ? 1.0 : 0.0

                Behavior on opacity {
                    NumberAnimation { duration: Theme.animFast }
                }

                AppIcon {
                    anchors.centerIn: parent
                    name: "chevron-left"
                    size: 18
                    color: "#FFFFFF"
                }

                MouseArea {
                    id: leftChevronMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.scrollByCards(-1)
                }
            }

            // Right scroll chevron button
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spaceSm
                y: Math.round(root._artHeight / 2 - height / 2) + 6
                width: 36
                height: 36
                radius: 18
                z: 10
                color: rightChevronMouse.containsMouse ? Theme.accent : Theme.bgOverlay
                border.width: 1
                border.color: rightChevronMouse.containsMouse ? "#FFFFFF" : Theme.borderStrong
                visible: !listView.atXEnd && listView.contentWidth > listView.width
                opacity: (carouselHover.hovered || rightChevronMouse.containsMouse) ? 1.0 : 0.0

                Behavior on opacity {
                    NumberAnimation { duration: Theme.animFast }
                }

                AppIcon {
                    anchors.centerIn: parent
                    name: "chevron-right"
                    size: 18
                    color: "#FFFFFF"
                }

                MouseArea {
                    id: rightChevronMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.scrollByCards(1)
                }
            }
        }
    }
}
