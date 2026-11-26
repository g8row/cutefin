import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Item {
    id: root

    property var videoItem: null
    readonly property double duration: videoItem ? Math.max(0, videoItem.duration) : 0
    readonly property double position: videoItem ? Math.max(0, videoItem.position) : 0
    readonly property double cacheDuration: videoItem ? Math.max(0, videoItem.cacheDuration) : 0

    property bool dragging: false
    property double dragPosition: 0
    property bool pendingSeekActive: false
    property double pendingSeekPosition: 0
    readonly property bool hovering: seekArea.containsMouse
    property real hoverX: 0
    property double hoverSeconds: 0
    property int hoverMs: 0
    property int lastRequestedTrickplayMs: -1

    signal userInteracted()

    implicitHeight: 28

    onVideoItemChanged: {
        pendingSeekActive = false
        pendingSeekTimer.stop()
    }

    readonly property double displayPosition: dragging
        ? dragPosition
        : (pendingSeekActive ? pendingSeekPosition : position)
    readonly property real progressRatio: duration > 0 ? Math.max(0.0, Math.min(1.0, displayPosition / duration)) : 0.0
    readonly property real bufferedRatio: (duration > 0 && cacheDuration > 0)
        ? Math.max(progressRatio, Math.min(1.0, (position + cacheDuration) / duration))
        : progressRatio
    readonly property string hoveredChapterName: findChapterName(hoverSeconds)

    Timer {
        id: pendingSeekTimer
        interval: 800
        repeat: false
        onTriggered: root.pendingSeekActive = false
    }

    Connections {
        target: root.videoItem
        function onSeeked() {
            root.pendingSeekActive = false
            pendingSeekTimer.stop()
        }
    }

    function chapterStartSeconds(ch) {
        if (!ch)
            return 0
        if (ch.startSeconds !== undefined)
            return Number(ch.startSeconds)
        if (ch.startTicks !== undefined)
            return Number(ch.startTicks) / 10000000.0
        return 0
    }

    function findChapterName(sec) {
        var list = player.chapters
        if (!list || list.length === 0)
            return ""
        var current = ""
        for (var i = 0; i < list.length; ++i) {
            var start = chapterStartSeconds(list[i])
            if (start <= sec + 0.25)
                current = list[i].name || ""
            else
                break
        }
        return current
    }

    function updateHoverFromX(mx, forceRequest) {
        if (width <= 0 || duration <= 0)
            return
        var clampedX = Math.max(0, Math.min(width, mx))
        hoverX = clampedX
        var ratio = clampedX / width
        var sec = ratio * duration
        hoverSeconds = sec
        var ms = Math.max(0, Math.round(sec * 1000))
        var bucketMs = Math.floor(ms / 1000) * 1000
        hoverMs = bucketMs
        if (dragging)
            dragPosition = sec
        if (player.trickplayEnabled && (forceRequest || bucketMs !== lastRequestedTrickplayMs)) {
            lastRequestedTrickplayMs = bucketMs
            player.requestTrickplay(bucketMs)
        }
    }

    // -----------------------------------------------------------------------
    // Floating Trickplay Thumbnail + Timestamp + Chapter Popup
    // -----------------------------------------------------------------------
    Rectangle {
        id: previewPopup
        visible: (root.hovering || root.dragging) && root.duration > 0
        z: 50
        width: player.trickplayEnabled ? 204 : Math.max(92, previewMeta.implicitWidth + 24)
        height: previewCol.implicitHeight + 12
        x: Math.max(0, Math.min(root.width - width, root.hoverX - width / 2))
        y: -height - 14
        radius: Theme.radiusMd
        color: Qt.rgba(0.05, 0.06, 0.09, 0.94)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.18)

        ColumnLayout {
            id: previewCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 6

            Rectangle {
                visible: player.trickplayEnabled
                Layout.fillWidth: true
                Layout.preferredHeight: 108
                radius: Theme.radiusSm
                color: "#090B10"
                clip: true

                Image {
                    id: trickplayImg
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    retainWhileLoading: true
                    smooth: true
                    source: (previewPopup.visible && player.trickplayEnabled)
                        ? ("image://trickplay/" + root.hoverMs + "/" + player.trickplayRevision)
                        : ""
                }

                // Subtle loading placeholder when tile hasn't arrived yet
                Text {
                    anchors.centerIn: parent
                    visible: trickplayImg.status !== Image.Ready && trickplayImg.paintedWidth <= 0
                    text: Theme.formatTime(root.hoverSeconds)
                    color: Qt.rgba(1, 1, 1, 0.35)
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSmall
                }
            }

            ColumnLayout {
                id: previewMeta
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: Theme.formatTime(root.hoverSeconds)
                    color: "#FFFFFF"
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    font.weight: Font.DemiBold
                }

                Text {
                    visible: root.hoveredChapterName.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    horizontalAlignment: Text.AlignHCenter
                    text: root.hoveredChapterName
                    color: Qt.rgba(1, 1, 1, 0.72)
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Seek Bar Track, Cache Fill, Gradient Progress Fill, Chapter Notches
    // -----------------------------------------------------------------------
    Rectangle {
        id: trackBg
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: (root.hovering || root.dragging) ? 7 : 4
        radius: height / 2
        color: Qt.rgba(1, 1, 1, 0.20)
        clip: true

        Behavior on height {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animFast }
        }

        // Buffered cache duration bar
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * root.bufferedRatio
            radius: parent.radius
            color: Qt.rgba(1, 1, 1, 0.30)
        }

        // Subtle hover target highlight
        Rectangle {
            visible: root.hovering && !root.dragging && root.duration > 0
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.max(0, Math.min(parent.width, root.hoverX))
            radius: parent.radius
            color: Qt.rgba(1, 1, 1, 0.14)
        }

        // Jellyfin purple -> cyan horizontal gradient progress fill
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * root.progressRatio
            radius: parent.radius

            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.accent }
                GradientStop { position: 1.0; color: Theme.accentSecondary }
            }
        }

        // Subtle chapter notch markers
        Repeater {
            model: player.chapters
            delegate: Rectangle {
                readonly property double startSec: root.chapterStartSeconds(modelData)
                readonly property real ratio: root.duration > 0 ? (startSec / root.duration) : 0
                visible: root.duration > 0 && ratio > 0.005 && ratio < 0.995
                x: Math.round(trackBg.width * ratio) - 1
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 2
                color: Qt.rgba(0.04, 0.05, 0.08, 0.85)
            }
        }
    }

    // Hover / Drag Thumb Handle
    Rectangle {
        id: thumbHandle
        width: 15
        height: 15
        radius: 7.5
        anchors.verticalCenter: trackBg.verticalCenter
        x: Math.max(0, Math.min(root.width - width, root.width * root.progressRatio - width / 2))
        color: "#FFFFFF"
        border.width: 2
        border.color: Theme.accent
        scale: (root.hovering || root.dragging) ? 1.0 : 0.0
        visible: scale > 0.01 && root.duration > 0

        Behavior on scale {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic }
        }
    }

    MouseArea {
        id: seekArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.duration > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor

        onEntered: {
            root.updateHoverFromX(mouseX, true)
            root.userInteracted()
        }

        onPressed: function(mouse) {
            if (root.duration <= 0)
                return
            root.pendingSeekActive = false
            pendingSeekTimer.stop()
            root.dragging = true
            root.updateHoverFromX(mouse.x, true)
            root.userInteracted()
        }

        onPositionChanged: function(mouse) {
            root.updateHoverFromX(mouse.x, false)
            if (root.dragging)
                root.userInteracted()
        }

        onReleased: function(mouse) {
            if (!root.dragging)
                return
            root.updateHoverFromX(mouse.x, false)
            var targetSec = root.dragPosition
            root.pendingSeekPosition = targetSec
            root.pendingSeekActive = true
            pendingSeekTimer.restart()
            root.dragging = false
            if (root.videoItem)
                root.videoItem.seek(targetSec)
            root.userInteracted()
        }

        onCanceled: {
            root.dragging = false
            root.pendingSeekActive = false
            pendingSeekTimer.stop()
        }
    }
}
