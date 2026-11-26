import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Cutefin

Item {
    id: root

    property var item
    property real cardWidth: 296

    signal activated(string itemId)
    signal playClicked(string itemId, bool resume)

    readonly property string itemId: (item && item.id) ? String(item.id) : ""
    readonly property string itemName: (item && item.name) ? String(item.name) : ""
    readonly property string itemType: (item && item.type) ? String(item.type) : ""
    readonly property string seriesName: (item && item.seriesName) ? String(item.seriesName) : ""
    readonly property string episodeCode: (item && item.episodeCode) ? String(item.episodeCode) : ""
    readonly property string subtitleText: (item && item.subtitleText) ? String(item.subtitleText) : ""
    readonly property string remainingText: (item && item.remainingText) ? String(item.remainingText) : ""
    readonly property string runtimeText: (item && item.runtimeText) ? String(item.runtimeText) : ""

    readonly property string thumbTag: (item && item.thumbTag) ? String(item.thumbTag) : ""
    readonly property string thumbBlurHash: (item && item.thumbBlurHash) ? String(item.thumbBlurHash) : ""
    readonly property string backdropTag: (item && item.backdropTag) ? String(item.backdropTag) : ""
    readonly property string backdropItemId: (item && item.backdropItemId) ? String(item.backdropItemId) : ""
    readonly property string backdropBlurHash: (item && item.backdropBlurHash) ? String(item.backdropBlurHash) : ""
    readonly property string primaryTag: (item && item.primaryTag) ? String(item.primaryTag) : ""
    readonly property string primaryBlurHash: (item && item.primaryBlurHash) ? String(item.primaryBlurHash) : ""

    readonly property bool isPlayed: Boolean(item && item.played)
    readonly property real playedPct: (item && item.playedPercentage !== undefined) ? Number(item.playedPercentage) : 0
    readonly property real playbackTicks: (item && item.playbackPositionTicks !== undefined) ? Number(item.playbackPositionTicks) : 0
    readonly property bool isPlayable: (item && item.isPlayable !== undefined) ? Boolean(item.isPlayable) : true
    readonly property bool canResume: playbackTicks > 0 || playedPct > 0

    readonly property int requestWidth: Math.max(200, Math.ceil((cardWidth * (Screen.devicePixelRatio || 1)) / 100) * 100)

    // Thumb -> Backdrop -> Primary fallback
    readonly property string resolvedImageUrl: {
        const w = requestWidth;
        if (thumbTag.length > 0 && itemId.length > 0)
            return Theme.imageUrl(itemId, "Thumb", thumbTag, w);
        if (backdropTag.length > 0 && (backdropItemId.length > 0 || itemId.length > 0))
            return Theme.imageUrl(backdropItemId.length > 0 ? backdropItemId : itemId, "Backdrop", backdropTag, w);
        if (primaryTag.length > 0 && itemId.length > 0)
            return Theme.imageUrl(itemId, "Primary", primaryTag, w);
        return "";
    }

    readonly property string resolvedBlurHashUrl: {
        if (thumbTag.length > 0 && thumbBlurHash.length > 0)
            return Theme.blurHashUrl(thumbBlurHash);
        if (backdropTag.length > 0 && backdropBlurHash.length > 0)
            return Theme.blurHashUrl(backdropBlurHash);
        if (primaryTag.length > 0 && primaryBlurHash.length > 0)
            return Theme.blurHashUrl(primaryBlurHash);
        return "";
    }

    readonly property string displayTitle: itemName
    readonly property string displaySubtitle: {
        if (seriesName.length > 0 && episodeCode.length > 0)
            return seriesName + " · " + episodeCode;
        if (seriesName.length > 0)
            return seriesName;
        return subtitleText;
    }
    readonly property string pillText: remainingText.length > 0 ? remainingText : runtimeText

    readonly property int thumbHeight: Math.round(cardWidth * 9 / 16)
    readonly property bool _hovered: cardMouse.containsMouse || playMouse.containsMouse
    readonly property bool _focused: activeFocus

    implicitWidth: cardWidth
    implicitHeight: thumbHeight + Theme.spaceSm + captionCol.implicitHeight
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
            root.playClicked(root.itemId, root.canResume);
        }
    }

    Column {
        anchors.fill: parent
        spacing: Theme.spaceSm

        // 16:9 Thumbnail frame
        Item {
            width: root.cardWidth
            height: root.thumbHeight
            scale: (root._hovered || root._focused) ? 1.03 : 1.0
            transformOrigin: Item.Center

            Behavior on scale {
                NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic }
            }

            Rectangle {
                id: thumbClip
                anchors.fill: parent
                radius: Theme.radiusSm + 2
                color: Theme.bgCard
                clip: true

                AppIcon {
                    anchors.centerIn: parent
                    size: 32
                    name: root.itemType === "Episode" ? "tv" : "film"
                    color: Theme.textMuted
                    visible: thumbImage.status !== Image.Ready && !blurImage.visible
                }

                Image {
                    id: blurImage
                    anchors.fill: parent
                    anchors.margins: 1
                    source: root.resolvedBlurHashUrl
                    asynchronous: false
                    smooth: true
                    fillMode: Image.PreserveAspectCrop
                    visible: source !== "" && thumbImage.status !== Image.Ready
                }

                Image {
                    id: thumbImage
                    anchors.fill: parent
                    anchors.margins: 1
                    source: root.resolvedImageUrl
                    asynchronous: true
                    cache: false
                    smooth: true
                    fillMode: Image.PreserveAspectCrop
                    opacity: status === Image.Ready ? 1.0 : 0.0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.animFast }
                    }
                }

                // Bottom gradient scrim
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 60
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: "#E60B0D12" }
                    }
                }

                // Hover/Focus dim scrim
                Rectangle {
                    anchors.fill: parent
                    color: "#000000"
                    opacity: (root._hovered || root._focused) ? 0.26 : 0.0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.animFast }
                    }
                }

                // Remaining time / runtime pill
                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: Theme.spaceSm
                    anchors.bottomMargin: root.playedPct > 0 ? 10 : Theme.spaceSm
                    visible: root.pillText.length > 0
                    implicitWidth: pillLabel.implicitWidth + 14
                    implicitHeight: 22
                    width: implicitWidth
                    height: implicitHeight
                    radius: height / 2
                    color: "#CC0B0D12"
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.14)

                    Label {
                        id: pillLabel
                        anchors.centerIn: parent
                        text: root.pillText
                        color: "#F3F5F9"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.Medium
                    }
                }

                // Progress bar at bottom
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 3.5
                    color: Qt.rgba(1, 1, 1, 0.22)
                    visible: root.playedPct > 0

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.min(1.0, Math.max(0.0, root.playedPct / 100.0))
                        color: Theme.accent
                    }
                }

                // Card click hit target (under Play button)
                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.itemId.length > 0)
                            root.activated(root.itemId);
                    }
                }

                // Center Play button on hover/focus
                Rectangle {
                    anchors.centerIn: parent
                    width: 48
                    height: 48
                    radius: 24
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

                // Hairline / accent frame
                Rectangle {
                    anchors.fill: parent
                    radius: thumbClip.radius
                    color: "transparent"
                    border.width: (root._hovered || root._focused) ? 2 : 1
                    border.color: (root._hovered || root._focused) ? Theme.accent : Theme.border

                    Behavior on border.color {
                        ColorAnimation { duration: Theme.animFast }
                    }
                }
            }
        }

        // Title + series/episode subtitle
        Column {
            id: captionCol
            width: root.cardWidth
            spacing: 2

            Label {
                width: parent.width
                text: root.displayTitle
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Label {
                width: parent.width
                visible: root.displaySubtitle.length > 0
                text: root.displaySubtitle
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }
}
