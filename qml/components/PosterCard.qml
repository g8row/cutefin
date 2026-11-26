import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Cutefin

Item {
    id: root

    property var item
    property real cardWidth: 164

    signal activated(string itemId)
    signal playClicked(string itemId, bool resume)
    signal contextRequested(var item)

    readonly property string itemId: (item && item.id) ? String(item.id) : ""
    readonly property string itemName: (item && item.name) ? String(item.name) : ""
    readonly property string itemType: (item && item.type) ? String(item.type) : ""
    readonly property string primaryTag: (item && item.primaryTag) ? String(item.primaryTag) : ""
    readonly property string primaryBlurHash: (item && item.primaryBlurHash) ? String(item.primaryBlurHash) : ""
    readonly property int year: (item && item.year) ? Number(item.year) : 0
    readonly property string episodeCode: (item && item.episodeCode) ? String(item.episodeCode) : ""
    readonly property string subtitleText: (item && item.subtitleText) ? String(item.subtitleText) : ""
    readonly property bool isPlayed: Boolean(item && item.played)
    readonly property real playedPct: (item && item.playedPercentage !== undefined) ? Number(item.playedPercentage) : 0
    readonly property real playbackTicks: (item && item.playbackPositionTicks !== undefined) ? Number(item.playbackPositionTicks) : 0
    readonly property bool isFavorite: Boolean(item && item.isFavorite)
    readonly property bool isPlayable: Boolean(item && item.isPlayable)
    readonly property real communityRating: (item && item.communityRating !== undefined) ? Number(item.communityRating) : -1
    readonly property bool canResume: playbackTicks > 0 || playedPct > 0

    readonly property int posterHeight: Math.round(cardWidth * 1.5)
    readonly property int requestWidth: Math.max(200, Math.ceil((cardWidth * (Screen.devicePixelRatio || 1)) / 100) * 100)
    readonly property string displaySubtitle: {
        if (episodeCode.length > 0)
            return episodeCode;
        if (year > 0)
            return String(year);
        return subtitleText;
    }

    readonly property bool _hovered: cardMouse.containsMouse || playMouse.containsMouse
    readonly property bool _focused: activeFocus

    implicitWidth: cardWidth
    implicitHeight: posterHeight + Theme.spaceSm + captionCol.implicitHeight
    width: cardWidth
    height: implicitHeight

    activeFocusOnTab: true

    Keys.onReturnPressed: function(event) {
        if (root.itemId.length > 0) {
            event.accepted = true;
            root.activated(root.itemId);
        }
    }
    Keys.onEnterPressed: function(event) {
        if (root.itemId.length > 0) {
            event.accepted = true;
            root.activated(root.itemId);
        }
    }
    Keys.onSpacePressed: function(event) {
        if (root.itemId.length > 0) {
            event.accepted = true;
            root.activated(root.itemId);
        }
    }

    Column {
        anchors.fill: parent
        spacing: Theme.spaceSm

        // 2:3 Poster frame
        Item {
            id: posterFrame
            width: root.cardWidth
            height: root.posterHeight
            scale: (root._hovered || root._focused) ? 1.03 : 1.0
            transformOrigin: Item.Center

            Behavior on scale {
                NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic }
            }

            Rectangle {
                id: posterClip
                anchors.fill: parent
                radius: Theme.radiusSm + 2
                color: Theme.bgCard
                clip: true

                // Fallback icon when no image is present
                AppIcon {
                    anchors.centerIn: parent
                    size: 32
                    name: (root.itemType === "Series" || root.itemType === "Season")
                        ? "tv"
                        : (root.itemType === "Person" ? "user" : "film")
                    color: Theme.textMuted
                    visible: posterImage.status !== Image.Ready && !blurImage.visible
                }

                // Instant BlurHash placeholder (tiny 16x16 upscaled on GPU)
                Image {
                    id: blurImage
                    anchors.fill: parent
                    anchors.margins: 1
                    source: Theme.blurHashUrl(root.primaryBlurHash)
                    asynchronous: false
                    smooth: true
                    fillMode: Image.PreserveAspectCrop
                    visible: source !== "" && posterImage.status !== Image.Ready
                }

                // Async Jellyfin Primary poster
                Image {
                    id: posterImage
                    anchors.fill: parent
                    anchors.margins: 1
                    source: Theme.imageUrl(root.itemId, "Primary", root.primaryTag, root.requestWidth)
                    asynchronous: true
                    cache: false
                    smooth: true
                    fillMode: Image.PreserveAspectCrop
                    opacity: status === Image.Ready ? 1.0 : 0.0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.animFast }
                    }
                }

                // Bottom scrim when progress bar is visible
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 28
                    visible: root.playedPct > 0 && !root.isPlayed
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: "#CC0B0D12" }
                    }
                }

                // Hover/Focus subtle cinema scrim
                Rectangle {
                    anchors.fill: parent
                    color: "#000000"
                    opacity: (root._hovered || root._focused) ? 0.28 : 0.0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.animFast }
                    }
                }

                // Favorite heart badge (top-left)
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: Theme.spaceSm
                    width: 24
                    height: 24
                    radius: 12
                    color: "#B30B0D12"
                    visible: root.isFavorite

                    AppIcon {
                        anchors.centerIn: parent
                        name: "heart-filled"
                        size: 13
                        color: "#F43F5E"
                    }
                }

                // Watched checkmark badge (top-right)
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: Theme.spaceSm
                    width: 24
                    height: 24
                    radius: 12
                    color: Theme.accent
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.25)
                    visible: root.isPlayed

                    AppIcon {
                        anchors.centerIn: parent
                        name: "check"
                        size: 13
                        color: "#FFFFFF"
                    }
                }

                // Progress bar at bottom
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 3.5
                    color: Qt.rgba(1, 1, 1, 0.22)
                    visible: root.playedPct > 0 && !root.isPlayed

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.min(1.0, Math.max(0.0, root.playedPct / 100.0))
                        color: Theme.accent
                    }
                }

                // Card click & context-menu hit target (under Play button)
                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.contextRequested(root.item);
                            contextMenu.popup();
                        } else if (root.itemId.length > 0) {
                            root.activated(root.itemId);
                        }
                    }
                }

                // Floating circular Play button on hover/focus
                Rectangle {
                    id: playButton
                    anchors.centerIn: parent
                    width: 46
                    height: 46
                    radius: 23
                    visible: root.isPlayable
                    opacity: (root._hovered || root._focused) ? 1.0 : 0.0
                    scale: playMouse.pressed ? 0.92 : (playMouse.containsMouse ? 1.08 : 1.0)
                    color: playMouse.containsMouse ? Theme.accent : "#CC0B0D12"
                    border.width: 1.5
                    border.color: playMouse.containsMouse ? "#FFFFFF" : Theme.accent

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.animFast }
                    }
                    Behavior on scale {
                        NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic }
                    }

                    AppIcon {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 1
                        name: "play"
                        size: 18
                        color: "#FFFFFF"
                    }

                    MouseArea {
                        id: playMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: root.isPlayable
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.itemId.length > 0)
                                root.playClicked(root.itemId, root.canResume);
                        }
                    }
                }

                // Crisp hairline / accent border (covers corners cleanly without shaders)
                Rectangle {
                    anchors.fill: parent
                    radius: posterClip.radius
                    color: "transparent"
                    border.width: (root._hovered || root._focused) ? 2 : 1
                    border.color: (root._hovered || root._focused) ? Theme.accent : Theme.border

                    Behavior on border.color {
                        ColorAnimation { duration: Theme.animFast }
                    }
                }
            }
        }

        // Caption below poster
        Column {
            id: captionCol
            width: root.cardWidth
            spacing: 2

            Label {
                width: parent.width
                text: root.itemName
                color: (root._hovered || root._focused) ? Theme.textPrimary : Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spaceXs
                visible: root.displaySubtitle.length > 0 || root.communityRating > 0

                Label {
                    Layout.fillWidth: true
                    text: root.displaySubtitle
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Row {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 3
                    visible: root.communityRating > 0

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "star"
                        size: 11
                        color: Theme.warning
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.communityRating.toFixed(1)
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.Medium
                    }
                }
            }
        }
    }

    // Inline right-click context menu
    Menu {
        id: contextMenu
        padding: 4

        background: Rectangle {
            implicitWidth: 196
            radius: Theme.radiusMd
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.borderStrong
        }

        delegate: MenuItem {
            id: menuDelegate
            implicitHeight: 34
            contentItem: Label {
                text: menuDelegate.text
                color: menuDelegate.highlighted ? "#FFFFFF" : Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
                rightPadding: 8
            }
            background: Rectangle {
                radius: Theme.radiusSm
                color: menuDelegate.highlighted ? Theme.accent : "transparent"
            }
        }

        MenuItem {
            text: "Resume"
            enabled: root.isPlayable && root.canResume
            height: enabled ? implicitHeight : 0
            visible: enabled
            onTriggered: root.playClicked(root.itemId, true)
        }

        MenuItem {
            text: root.canResume ? "Play from Beginning" : "Play"
            enabled: root.isPlayable
            height: enabled ? implicitHeight : 0
            visible: enabled
            onTriggered: root.playClicked(root.itemId, false)
        }

        MenuItem {
            text: "Details"
            onTriggered: root.activated(root.itemId)
        }

        MenuItem {
            text: root.isPlayed ? "Mark Unwatched" : "Mark Watched"
            visible: root.itemId.length > 0 && root.itemType !== "Person"
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (typeof app !== "undefined" && app)
                    app.togglePlayed(root.itemId, root.isPlayed);
            }
        }

        MenuItem {
            text: root.isFavorite ? "Remove from Favorites" : "Add to Favorites"
            visible: root.itemId.length > 0
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (typeof app !== "undefined" && app)
                    app.toggleFavorite(root.itemId, root.isFavorite);
            }
        }
    }
}
