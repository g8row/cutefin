import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Cutefin

FocusScope {
    id: root

    property var targetWindow: null
    property bool controlsVisible: true
    property bool drawerOpen: false
    property string drawerTab: "tracks"
    property bool hoveringTopBar: false
    property bool hoveringDock: false
    property string osdMessage: ""
    property bool osdVisible: false
    property real wheelAccum: 0

    readonly property bool isFullScreen: targetWindow ? (targetWindow.visibility === Window.FullScreen) : false
    readonly property bool keepControlsVisible:
        mpvVideo.paused ||
        player.loading ||
        mpvVideo.buffering ||
        drawerOpen ||
        hoveringTopBar ||
        hoveringDock ||
        seekBar.dragging ||
        seekBar.hovering ||
        volumeMouseArea.pressed
    readonly property var speedSteps: [0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0]

    focus: player.open

    Component.onCompleted: {
        if (player.open)
            root.revealControls()
    }

    Component.onDestruction: {
        if (player.videoItem === mpvVideo)
            player.videoItem = null
    }

    onDrawerOpenChanged: {
        if (!drawerOpen && player.open)
            root.forceActiveFocus()
    }

    // Lightweight inline SVG icon component (zero external SVG filename dependencies)
    component CinemaIcon: Image {
        property string pathData: ""
        property color iconColor: "#FFFFFF"
        property int iconSize: 18
        property real strokeWidth: 2.0
        property bool filled: false

        width: iconSize
        height: iconSize
        sourceSize: Qt.size(iconSize * 2, iconSize * 2)
        smooth: true
        mipmap: false
        asynchronous: false
        source: {
            var c = iconColor.toString()
            var fillAttr = filled ? c : "none"
            var strokeAttr = filled ? "none" : c
            var svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='" + fillAttr
                + "' stroke='" + strokeAttr + "' stroke-width='" + strokeWidth
                + "' stroke-linecap='round' stroke-linejoin='round'>" + pathData + "</svg>"
            return "data:image/svg+xml," + encodeURIComponent(svg)
        }
    }

    component CinemaIconButton: Rectangle {
        id: btnRoot
        property string pathData: ""
        property string tooltipText: ""
        property int buttonSize: 36
        property int iconSize: 18
        property bool active: false
        property bool filledIcon: false
        property color iconColor: active ? Theme.accent : "#F4F6FA"
        signal clicked()

        implicitWidth: buttonSize
        implicitHeight: buttonSize
        radius: buttonSize / 2
        color: btnArea.pressed
            ? Qt.rgba(1, 1, 1, 0.22)
            : (btnArea.containsMouse
                ? Qt.rgba(1, 1, 1, 0.14)
                : (active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22) : "transparent"))
        border.width: active ? 1 : 0
        border.color: active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.65) : "transparent"

        Behavior on color {
            enabled: !app.reduceMotion
            ColorAnimation { duration: Theme.animFast }
        }

        CinemaIcon {
            anchors.centerIn: parent
            pathData: btnRoot.pathData
            iconSize: btnRoot.iconSize
            iconColor: btnRoot.iconColor
            filled: btnRoot.filledIcon
        }

        MouseArea {
            id: btnArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.revealControls()
                btnRoot.clicked()
            }
        }

        ToolTip.visible: btnArea.containsMouse && tooltipText.length > 0
        ToolTip.delay: 450
        ToolTip.text: tooltipText
    }

    function revealControls() {
        controlsVisible = true
        if (!keepControlsVisible && player.open)
            hideTimer.restart()
        else
            hideTimer.stop()
    }

    function showOsd(text) {
        osdMessage = text
        osdVisible = true
        osdTimer.restart()
    }

    function toggleDrawer(tabName) {
        if (drawerOpen && drawerTab === tabName) {
            drawerOpen = false
        } else {
            drawerTab = tabName
            drawerOpen = true
            if (tabName === "audio")
                player.refreshAudioDevices()
        }
        revealControls()
    }

    function toggleFullscreen() {
        if (targetWindow && typeof targetWindow.togglePlayerFullscreen === "function") {
            targetWindow.togglePlayerFullscreen()
        } else if (targetWindow) {
            targetWindow.visibility = (targetWindow.visibility === Window.FullScreen)
                ? Window.AutomaticVisibility
                : Window.FullScreen
        }
    }

    function exitFullscreen() {
        if (targetWindow && typeof targetWindow.exitPlayerFullscreen === "function") {
            targetWindow.exitPlayerFullscreen()
        } else if (targetWindow && targetWindow.visibility === Window.FullScreen) {
            targetWindow.visibility = Window.AutomaticVisibility
        }
    }

    function stepSpeed(delta) {
        var cur = mpvVideo.speed
        var bestIdx = 2
        var bestDiff = 999
        for (var i = 0; i < speedSteps.length; ++i) {
            var d = Math.abs(speedSteps[i] - cur)
            if (d < bestDiff) {
                bestDiff = d
                bestIdx = i
            }
        }
        var nextIdx = Math.max(0, Math.min(speedSteps.length - 1, bestIdx + delta))
        var nextVal = speedSteps[nextIdx]
        mpvVideo.setSpeed(nextVal)
        showOsd("Speed " + formatSpeed(nextVal))
    }

    function cycleSpeed() {
        var cur = mpvVideo.speed
        var nextIdx = 0
        for (var i = 0; i < speedSteps.length; ++i) {
            if (speedSteps[i] > cur + 0.01) {
                nextIdx = i
                break
            }
        }
        var nextVal = speedSteps[nextIdx]
        mpvVideo.setSpeed(nextVal)
        showOsd("Speed " + formatSpeed(nextVal))
    }

    function formatSpeed(spd) {
        var rounded = Math.round(spd * 100) / 100
        return (rounded === Math.floor(rounded) ? rounded.toFixed(0) : rounded.toString()) + "×"
    }

    function adjustVolume(delta) {
        var nextVol = Math.max(0, Math.min(100, mpvVideo.volume + delta))
        if (mpvVideo.muted && delta > 0)
            mpvVideo.setMuted(false)
        mpvVideo.setVolume(nextVol)
        showOsd("Volume " + nextVol + "%")
    }

    onKeepControlsVisibleChanged: {
        if (keepControlsVisible) {
            controlsVisible = true
            hideTimer.stop()
        } else if (player.open) {
            hideTimer.restart()
        }
    }

    Connections {
        target: player
        function onOpenChanged() {
            if (player.open) {
                root.drawerOpen = false
                root.revealControls()
                root.forceActiveFocus()
            } else {
                root.drawerOpen = false
                root.osdVisible = false
                hideTimer.stop()
                osdTimer.stop()
                singleClickTimer.stop()
                wheelResetTimer.stop()
                root.wheelAccum = 0
            }
        }
    }

    Timer {
        id: wheelResetTimer
        interval: 400
        repeat: false
        onTriggered: root.wheelAccum = 0
    }

    Timer {
        id: hideTimer
        interval: 2500
        repeat: false
        onTriggered: {
            if (!root.keepControlsVisible && player.open)
                root.controlsVisible = false
        }
    }

    Timer {
        id: osdTimer
        interval: 1200
        repeat: false
        onTriggered: root.osdVisible = false
    }

    Timer {
        id: singleClickTimer
        interval: 210
        repeat: false
        onTriggered: {
            var willPause = !mpvVideo.paused
            mpvVideo.togglePause()
            root.showOsd(willPause ? "Paused" : "Playing")
            root.revealControls()
        }
    }

    // -----------------------------------------------------------------------
    // Full Keyboard Shortcuts (active when player is open)
    // -----------------------------------------------------------------------
    Keys.onPressed: function(event) {
        if (!player.open)
            return

        var shift = (event.modifiers & Qt.ShiftModifier) !== 0
        var ctrl = (event.modifiers & Qt.ControlModifier) !== 0

        switch (event.key) {
        case Qt.Key_Space:
        case Qt.Key_K: {
            var willPause = !mpvVideo.paused
            mpvVideo.togglePause()
            showOsd(willPause ? "Paused" : "Playing")
            revealControls()
            event.accepted = true
            break
        }
        case Qt.Key_Left: {
            var backSec = shift ? -60 : -5
            mpvVideo.seekRelative(backSec)
            showOsd(backSec + "s")
            revealControls()
            event.accepted = true
            break
        }
        case Qt.Key_Right: {
            var fwdSec = shift ? 60 : 5
            mpvVideo.seekRelative(fwdSec)
            showOsd("+" + fwdSec + "s")
            revealControls()
            event.accepted = true
            break
        }
        case Qt.Key_J:
            mpvVideo.seekRelative(-10)
            showOsd("-10s")
            revealControls()
            event.accepted = true
            break
        case Qt.Key_L:
            mpvVideo.seekRelative(10)
            showOsd("+10s")
            revealControls()
            event.accepted = true
            break
        case Qt.Key_Up:
            adjustVolume(5)
            revealControls()
            event.accepted = true
            break
        case Qt.Key_Down:
            adjustVolume(-5)
            revealControls()
            event.accepted = true
            break
        case Qt.Key_M: {
            var willMute = !mpvVideo.muted
            mpvVideo.setMuted(willMute)
            showOsd(willMute ? "Muted" : ("Volume " + mpvVideo.volume + "%"))
            revealControls()
            event.accepted = true
            break
        }
        case Qt.Key_F:
            toggleFullscreen()
            revealControls()
            event.accepted = true
            break
        case Qt.Key_I:
            player.statsVisible = !player.statsVisible
            event.accepted = true
            break
        case Qt.Key_D:
            if (ctrl && shift) {
                player.statsVisible = !player.statsVisible
                event.accepted = true
            }
            break
        case Qt.Key_BracketLeft:
            stepSpeed(-1)
            revealControls()
            event.accepted = true
            break
        case Qt.Key_BracketRight:
            stepSpeed(1)
            revealControls()
            event.accepted = true
            break
        case Qt.Key_PageUp:
            if (player.chapters.length > 0) {
                player.jumpChapter(-1)
                showOsd("Previous Chapter")
                revealControls()
            }
            event.accepted = true
            break
        case Qt.Key_PageDown:
            if (player.chapters.length > 0) {
                player.jumpChapter(1)
                showOsd("Next Chapter")
                revealControls()
            }
            event.accepted = true
            break
        case Qt.Key_Z:
            player.adjustSubtitleDelay(-0.1)
            showOsd("Subtitle Delay " + (player.subtitleDelay >= 0 ? "+" : "") + player.subtitleDelay.toFixed(1) + "s")
            revealControls()
            event.accepted = true
            break
        case Qt.Key_X:
            player.adjustSubtitleDelay(0.1)
            showOsd("Subtitle Delay " + (player.subtitleDelay >= 0 ? "+" : "") + player.subtitleDelay.toFixed(1) + "s")
            revealControls()
            event.accepted = true
            break
        case Qt.Key_Period:
            mpvVideo.frameStep()
            showOsd("Frame +1")
            event.accepted = true
            break
        case Qt.Key_Comma:
            mpvVideo.frameBackStep()
            showOsd("Frame -1")
            event.accepted = true
            break
        case Qt.Key_Escape:
            if (drawerOpen) {
                drawerOpen = false
                revealControls()
            } else if (isFullScreen) {
                exitFullscreen()
                revealControls()
            } else {
                player.stopAndClose()
            }
            event.accepted = true
            break
        default:
            break
        }
    }

    // -----------------------------------------------------------------------
    // Black cinema canvas + embedded OpenGL libmpv video item
    // -----------------------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    MpvVideoItem {
        id: mpvVideo
        anchors.fill: parent
        Component.onCompleted: player.videoItem = mpvVideo
    }

    // Full-surface mouse interaction & cursor auto-hide
    MouseArea {
        id: videoSurfaceArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: (!root.controlsVisible && !root.keepControlsVisible && player.open)
            ? Qt.BlankCursor
            : Qt.ArrowCursor

        onPositionChanged: root.revealControls()

        onClicked: {
            root.forceActiveFocus()
            if (root.drawerOpen) {
                root.drawerOpen = false
                root.revealControls()
                return
            }
            singleClickTimer.restart()
        }

        onDoubleClicked: {
            singleClickTimer.stop()
            root.toggleFullscreen()
            root.revealControls()
        }

        onWheel: function(wheel) {
            var delta = wheel.angleDelta.y
            if (delta === 0)
                return
            if ((root.wheelAccum > 0 && delta < 0) || (root.wheelAccum < 0 && delta > 0))
                root.wheelAccum = 0
            root.wheelAccum += delta
            wheelResetTimer.restart()
            var steps = Math.trunc(root.wheelAccum / 120)
            if (steps !== 0) {
                root.wheelAccum -= steps * 120
                root.adjustVolume(steps * 5)
                root.revealControls()
            }
        }
    }

    // -----------------------------------------------------------------------
    // Center Buffering / Loading Spinner
    // -----------------------------------------------------------------------
    Rectangle {
        id: bufferingBadge
        anchors.centerIn: parent
        visible: player.loading || mpvVideo.buffering
        width: 72
        height: 72
        radius: 36
        color: Qt.rgba(0.05, 0.06, 0.09, 0.78)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)
        z: 10

        Canvas {
            id: spinnerCanvas
            width: 38
            height: 38
            anchors.centerIn: parent

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var cx = width / 2
                var cy = height / 2
                var r = width / 2 - 3

                ctx.beginPath()
                ctx.lineWidth = 3
                ctx.strokeStyle = "rgba(255, 255, 255, 0.16)"
                ctx.arc(cx, cy, r, 0, Math.PI * 2)
                ctx.stroke()

                ctx.beginPath()
                ctx.lineWidth = 3
                ctx.lineCap = "round"
                ctx.strokeStyle = Theme.accent.toString()
                ctx.arc(cx, cy, r, -Math.PI / 2, Math.PI * 0.55)
                ctx.stroke()
            }

            RotationAnimator on rotation {
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
                running: bufferingBadge.visible && !app.reduceMotion
            }
        }
    }

    // -----------------------------------------------------------------------
    // Subtle Center-Top OSD Feedback Pill
    // -----------------------------------------------------------------------
    Rectangle {
        id: osdPill
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 82
        z: 15
        visible: opacity > 0.01
        opacity: root.osdVisible ? 1.0 : 0.0
        implicitWidth: osdLabel.implicitWidth + 28
        implicitHeight: 34
        radius: 17
        color: Qt.rgba(0.06, 0.07, 0.11, 0.86)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.16)

        Behavior on opacity {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animFast }
        }

        Text {
            id: osdLabel
            anchors.centerIn: parent
            text: root.osdMessage
            color: "#FFFFFF"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
        }
    }

    // -----------------------------------------------------------------------
    // Top Gradient Scrim + Header Controls
    // -----------------------------------------------------------------------
    Rectangle {
        id: topBarOverlay
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: root.drawerOpen ? playerDrawer.width : 0
        anchors.top: parent.top
        height: 96
        z: 20
        opacity: root.controlsVisible ? 1.0 : 0.0
        visible: opacity > 0.01
        enabled: root.controlsVisible

        Behavior on anchors.rightMargin {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic }
        }

        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.84) }
            GradientStop { position: 0.65; color: Qt.rgba(0, 0, 0, 0.42) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
        }

        Behavior on opacity {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animNormal }
        }

        HoverHandler {
            onHoveredChanged: root.hoveringTopBar = hovered
        }

        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 16
            spacing: 12

            CinemaIconButton {
                pathData: "<path d='M19 12H5'/><path d='M12 19l-7-7 7-7'/>"
                tooltipText: "Back (Esc)"
                buttonSize: 40
                iconSize: 20
                onClicked: player.stopAndClose()
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: player.title.length > 0 ? player.title : "Now Playing"
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontH2
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.maximumWidth: Math.max(160, topBarOverlay.width - 440)
                    }

                    Rectangle {
                        visible: player.transcoding
                        implicitWidth: transcodeBadgeText.implicitWidth + 14
                        implicitHeight: 20
                        radius: 10
                        color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.22)
                        border.width: 1
                        border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.65)

                        Text {
                            id: transcodeBadgeText
                            anchors.centerIn: parent
                            text: "Transcode"
                            color: Theme.warning
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }

                Text {
                    visible: player.subtitle.length > 0
                    text: player.subtitle
                    color: Qt.rgba(1, 1, 1, 0.72)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Item {
                Layout.fillWidth: true
            }

            // Aspect Mode pill
            Rectangle {
                id: aspectPill
                implicitWidth: aspectRow.implicitWidth + 20
                implicitHeight: 32
                radius: 16
                color: aspectArea.pressed
                    ? Qt.rgba(1, 1, 1, 0.24)
                    : (aspectArea.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.09))
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.16)

                RowLayout {
                    id: aspectRow
                    anchors.centerIn: parent
                    spacing: 6

                    CinemaIcon {
                        pathData: "<rect x='3' y='5' width='18' height='14' rx='2'/><path d='M7 9h2v2'/><path d='M17 15h-2v-2'/>"
                        iconSize: 14
                        iconColor: "#E8ECF5"
                    }

                    Text {
                        text: mpvVideo.aspectMode
                        color: "#F4F6FA"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                    }
                }

                MouseArea {
                    id: aspectArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        mpvVideo.cycleAspectMode()
                        root.showOsd("Aspect: " + mpvVideo.aspectMode)
                        root.revealControls()
                    }
                }

                ToolTip.visible: aspectArea.containsMouse
                ToolTip.delay: 450
                ToolTip.text: "Cycle Aspect Mode"
            }

            CinemaIconButton {
                pathData: "<path d='M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z'/><circle cx='12' cy='13' r='4'/>"
                tooltipText: "Take Screenshot"
                onClicked: player.takeScreenshot()
            }

            CinemaIconButton {
                pathData: "<path d='M3 3v18h18'/><path d='M18 17V9'/><path d='M13 17V5'/><path d='M8 17v-3'/>"
                tooltipText: "Stats & Diagnostics (I)"
                active: player.statsVisible
                onClicked: player.statsVisible = !player.statsVisible
            }

            CinemaIconButton {
                pathData: root.isFullScreen
                    ? "<path d='M8 3v3a2 2 0 0 1-2 2H3'/><path d='M21 8h-3a2 2 0 0 1-2-2V3'/><path d='M3 16h3a2 2 0 0 1 2 2v3'/><path d='M16 21v-3a2 2 0 0 1 2-2h3'/>"
                    : "<path d='M8 3H5a2 2 0 0 0-2 2v3'/><path d='M21 8V5a2 2 0 0 0-2-2h-3'/><path d='M3 16v3a2 2 0 0 0 2 2h3'/><path d='M16 21h3a2 2 0 0 0 2-2v-3'/>"
                tooltipText: root.isFullScreen ? "Exit Fullscreen (F)" : "Fullscreen (F)"
                onClicked: root.toggleFullscreen()
            }
        }
    }

    // -----------------------------------------------------------------------
    // Bottom Cinema Gradient Scrim + Floating Dark Control Dock
    // -----------------------------------------------------------------------
    Rectangle {
        id: bottomScrim
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 156
        z: 20
        opacity: root.controlsVisible ? 1.0 : 0.0
        visible: opacity > 0.01
        enabled: root.controlsVisible

        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.0) }
            GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.55) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.88) }
        }

        Behavior on opacity {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animNormal }
        }

        Rectangle {
            id: controlDock
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: root.drawerOpen ? (playerDrawer.width + 20) : 20
            anchors.bottomMargin: 18
            height: dockLayout.implicitHeight + 20
            radius: Theme.radiusLg
            color: Qt.rgba(0.06, 0.07, 0.11, 0.90)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            Behavior on anchors.rightMargin {
                enabled: !app.reduceMotion
                NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic }
            }

            HoverHandler {
                onHoveredChanged: root.hoveringDock = hovered
            }

            // Swallow background clicks on dock so they don't pause video
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
            }

            ColumnLayout {
                id: dockLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 6

                PlayerSeekBar {
                    id: seekBar
                    Layout.fillWidth: true
                    videoItem: mpvVideo
                    onUserInteracted: root.revealControls()
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    // Primary Play/Pause button
                    Rectangle {
                        id: playPauseBtn
                        implicitWidth: 40
                        implicitHeight: 40
                        radius: 20
                        color: playArea.pressed
                            ? Theme.accentHover
                            : (playArea.containsMouse ? Theme.accentHover : Theme.accent)

                        Behavior on color {
                            enabled: !app.reduceMotion
                            ColorAnimation { duration: Theme.animFast }
                        }

                        CinemaIcon {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: mpvVideo.paused ? 1 : 0
                            pathData: mpvVideo.paused
                                ? "<polygon points='6 4 20 12 6 20 6 4'/>"
                                : "<rect x='6' y='4' width='4' height='16' rx='1'/><rect x='14' y='4' width='4' height='16' rx='1'/>"
                            iconSize: 18
                            iconColor: "#FFFFFF"
                            filled: true
                        }

                        MouseArea {
                            id: playArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                mpvVideo.togglePause()
                                root.revealControls()
                            }
                        }

                        ToolTip.visible: playArea.containsMouse
                        ToolTip.delay: 450
                        ToolTip.text: mpvVideo.paused ? "Play (Space)" : "Pause (Space)"
                    }

                    // Skip -10s
                    CinemaIconButton {
                        pathData: "<path d='M3 12a9 9 0 1 0 3-6.7L3 8'/><path d='M3 3v5h5'/><path d='M9.5 15V10.5L8.2 11.4' stroke-width='1.6'/><rect x='12.2' y='10.5' width='3.2' height='4.5' rx='1.4' stroke-width='1.6'/>"
                        tooltipText: "Back 10s (J)"
                        onClicked: {
                            mpvVideo.seekRelative(-10)
                            root.showOsd("-10s")
                        }
                    }

                    // Skip +10s
                    CinemaIconButton {
                        pathData: "<path d='M21 12a9 9 0 1 1-3-6.7L21 8'/><path d='M21 3v5h-5'/><path d='M9.5 15V10.5L8.2 11.4' stroke-width='1.6'/><rect x='12.2' y='10.5' width='3.2' height='4.5' rx='1.4' stroke-width='1.6'/>"
                        tooltipText: "Forward 10s (L)"
                        onClicked: {
                            mpvVideo.seekRelative(10)
                            root.showOsd("+10s")
                        }
                    }

                    // Prev / Next Chapter (when chapters exist)
                    CinemaIconButton {
                        visible: player.chapters.length > 0
                        pathData: "<polygon points='19 20 9 12 19 4 19 20'/><line x1='5' y1='19' x2='5' y2='5'/>"
                        tooltipText: "Previous Chapter (PgUp)"
                        iconSize: 16
                        onClicked: {
                            player.jumpChapter(-1)
                            root.showOsd("Previous Chapter")
                        }
                    }

                    CinemaIconButton {
                        visible: player.chapters.length > 0
                        pathData: "<polygon points='5 4 15 12 5 20 5 4'/><line x1='19' y1='5' x2='19' y2='19'/>"
                        tooltipText: "Next Chapter (PgDn)"
                        iconSize: 16
                        onClicked: {
                            player.jumpChapter(1)
                            root.showOsd("Next Chapter")
                        }
                    }

                    // Volume mute + sleek slider
                    RowLayout {
                        spacing: 4
                        Layout.leftMargin: 4

                        CinemaIconButton {
                            pathData: (mpvVideo.muted || mpvVideo.volume === 0)
                                ? "<polygon points='11 5 6 9 2 9 2 15 6 15 11 19 11 5'/><line x1='23' y1='9' x2='17' y2='15'/><line x1='17' y1='9' x2='23' y2='15'/>"
                                : (mpvVideo.volume < 50
                                    ? "<polygon points='11 5 6 9 2 9 2 15 6 15 11 19 11 5'/><path d='M15.54 8.46a5 5 0 0 1 0 7.07'/>"
                                    : "<polygon points='11 5 6 9 2 9 2 15 6 15 11 19 11 5'/><path d='M15.54 8.46a5 5 0 0 1 0 7.07'/><path d='M19.07 4.93a10 10 0 0 1 0 14.14'/>")
                            tooltipText: mpvVideo.muted ? "Unmute (M)" : "Mute (M)"
                            iconSize: 17
                            onClicked: mpvVideo.setMuted(!mpvVideo.muted)
                        }

                        Item {
                            id: volumeSlider
                            implicitWidth: 84
                            implicitHeight: 28

                            readonly property real effectiveLevel: mpvVideo.muted ? 0.0 : Math.max(0.0, Math.min(1.0, mpvVideo.volume / 100.0))

                            Rectangle {
                                id: volTrack
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                height: volumeMouseArea.containsMouse || volumeMouseArea.pressed ? 5 : 4
                                radius: height / 2
                                color: Qt.rgba(1, 1, 1, 0.22)

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: parent.width * volumeSlider.effectiveLevel
                                    radius: parent.radius
                                    color: mpvVideo.muted ? Qt.rgba(1, 1, 1, 0.4) : Theme.accent
                                }
                            }

                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                anchors.verticalCenter: volTrack.verticalCenter
                                x: Math.max(0, Math.min(volTrack.width - width, volTrack.width * volumeSlider.effectiveLevel - width / 2))
                                color: "#FFFFFF"
                                scale: (volumeMouseArea.containsMouse || volumeMouseArea.pressed) ? 1.0 : 0.78

                                Behavior on scale {
                                    enabled: !app.reduceMotion
                                    NumberAnimation { duration: Theme.animFast }
                                }
                            }

                            MouseArea {
                                id: volumeMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor

                                function updateFromX(mx) {
                                    var ratio = Math.max(0.0, Math.min(1.0, mx / width))
                                    var pct = Math.round(ratio * 100)
                                    if (mpvVideo.muted && pct > 0)
                                        mpvVideo.setMuted(false)
                                    mpvVideo.setVolume(pct)
                                    root.revealControls()
                                }

                                onPressed: function(mouse) { updateFromX(mouse.x) }
                                onPositionChanged: function(mouse) {
                                    if (pressed)
                                        updateFromX(mouse.x)
                                }
                            }
                        }
                    }

                    // Elapsed / Total time
                    Text {
                        Layout.leftMargin: 8
                        text: Theme.formatTime(seekBar.displayPosition)
                            + "  /  "
                            + Theme.formatTime(mpvVideo.duration)
                        color: "#E8ECF5"
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.Medium
                    }

                    Item { Layout.fillWidth: true }

                    // Drawer tab shortcuts: Chapters, Tracks, Sub Style, Audio, Quality
                    CinemaIconButton {
                        visible: player.chapters.length > 0
                        pathData: "<line x1='8' y1='6' x2='21' y2='6'/><line x1='8' y1='12' x2='21' y2='12'/><line x1='8' y1='18' x2='21' y2='18'/><circle cx='4' cy='6' r='1'/><circle cx='4' cy='12' r='1'/><circle cx='4' cy='18' r='1'/>"
                        tooltipText: "Chapters"
                        active: root.drawerOpen && root.drawerTab === "chapters"
                        onClicked: root.toggleDrawer("chapters")
                    }

                    CinemaIconButton {
                        pathData: "<rect x='2' y='5' width='20' height='14' rx='2'/><path d='M7 15h4'/><path d='M15 15h2'/><path d='M7 11h2'/><path d='M13 11h4'/>"
                        tooltipText: "Audio & Subtitle Tracks"
                        active: root.drawerOpen && root.drawerTab === "tracks"
                        onClicked: root.toggleDrawer("tracks")
                    }

                    CinemaIconButton {
                        pathData: "<polyline points='4 7 4 4 20 4 20 7'/><line x1='9' y1='20' x2='15' y2='20'/><line x1='12' y1='4' x2='12' y2='20'/>"
                        tooltipText: "Subtitle Appearance & Sync"
                        iconSize: 16
                        active: root.drawerOpen && root.drawerTab === "style"
                        onClicked: root.toggleDrawer("style")
                    }

                    CinemaIconButton {
                        pathData: "<line x1='4' y1='21' x2='4' y2='14'/><line x1='4' y1='10' x2='4' y2='3'/><line x1='12' y1='21' x2='12' y2='12'/><line x1='12' y1='8' x2='12' y2='3'/><line x1='20' y1='21' x2='20' y2='16'/><line x1='20' y1='12' x2='20' y2='3'/><line x1='1' y1='14' x2='7' y2='14'/><line x1='9' y1='8' x2='15' y2='8'/><line x1='17' y1='16' x2='23' y2='16'/>"
                        tooltipText: "Audio Output & Sync"
                        iconSize: 17
                        active: root.drawerOpen && root.drawerTab === "audio"
                        onClicked: root.toggleDrawer("audio")
                    }

                    // Speed pill (left-click cycles 0.5x..2.0x, right-click resets to 1x)
                    Rectangle {
                        id: speedPill
                        implicitWidth: speedText.implicitWidth + 18
                        implicitHeight: 30
                        radius: 15
                        color: speedArea.pressed
                            ? Qt.rgba(1, 1, 1, 0.22)
                            : (speedArea.containsMouse
                                ? Qt.rgba(1, 1, 1, 0.14)
                                : (Math.abs(mpvVideo.speed - 1.0) > 0.01
                                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.24)
                                    : Qt.rgba(1, 1, 1, 0.08)))
                        border.width: 1
                        border.color: Math.abs(mpvVideo.speed - 1.0) > 0.01
                            ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)
                            : Qt.rgba(1, 1, 1, 0.14)

                        Text {
                            id: speedText
                            anchors.centerIn: parent
                            text: root.formatSpeed(mpvVideo.speed)
                            color: Math.abs(mpvVideo.speed - 1.0) > 0.01 ? Theme.accent : "#F4F6FA"
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontCaption
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            id: speedArea
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    mpvVideo.setSpeed(1.0)
                                    root.showOsd("Speed 1×")
                                } else {
                                    root.cycleSpeed()
                                }
                                root.revealControls()
                            }
                        }

                        ToolTip.visible: speedArea.containsMouse
                        ToolTip.delay: 450
                        ToolTip.text: "Playback Speed ([ / ], right-click to reset)"
                    }

                    // Quality / Settings drawer button
                    CinemaIconButton {
                        pathData: "<circle cx='12' cy='12' r='3'/><path d='M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z'/>"
                        tooltipText: "Quality & Playback Speed"
                        active: root.drawerOpen && root.drawerTab === "quality"
                        onClicked: root.toggleDrawer("quality")
                    }

                    // Fullscreen button
                    CinemaIconButton {
                        pathData: root.isFullScreen
                            ? "<path d='M8 3v3a2 2 0 0 1-2 2H3'/><path d='M21 8h-3a2 2 0 0 1-2-2V3'/><path d='M3 16h3a2 2 0 0 1 2 2v3'/><path d='M16 21v-3a2 2 0 0 1 2-2h3'/>"
                            : "<path d='M8 3H5a2 2 0 0 0-2 2v3'/><path d='M21 8V5a2 2 0 0 0-2-2h-3'/><path d='M3 16v3a2 2 0 0 0 2 2h3'/><path d='M16 21h3a2 2 0 0 0 2-2v-3'/>"
                        tooltipText: root.isFullScreen ? "Exit Fullscreen (F)" : "Fullscreen (F)"
                        onClicked: root.toggleFullscreen()
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Skip Intro / Outro Pill & Next Episode Countdown Card
    // -----------------------------------------------------------------------
    SkipAndUpNextOverlay {
        id: skipAndUpNext
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: root.drawerOpen ? (playerDrawer.width + 24) : 24
        anchors.bottomMargin: root.controlsVisible ? (controlDock.height + 34) : 28
        z: 25

        Behavior on anchors.rightMargin {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic }
        }
        Behavior on anchors.bottomMargin {
            enabled: !app.reduceMotion
            NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic }
        }
    }

    // -----------------------------------------------------------------------
    // Stats & Diagnostics Overlay (top-left)
    // -----------------------------------------------------------------------
    StatsOverlay {
        id: statsOverlay
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 20
        anchors.topMargin: 84
        maxHeight: Math.max(240, root.height - 230)
        videoItem: mpvVideo
        z: 30
    }

    // -----------------------------------------------------------------------
    // Slide-Over Settings / Tracks / Chapters Drawer (right-hand 360px)
    // -----------------------------------------------------------------------
    PlayerDrawer {
        id: playerDrawer
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        z: 40
        open: root.drawerOpen
        activeTab: root.drawerTab
        videoItem: mpvVideo
        onTabSelected: function(tabName) {
            root.drawerTab = tabName
            if (tabName === "audio")
                player.refreshAudioDevices()
        }
        onCloseRequested: {
            root.drawerOpen = false
            root.forceActiveFocus()
        }
    }
}
