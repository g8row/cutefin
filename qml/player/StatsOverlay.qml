import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root

    property var videoItem: null
    property int maxHeight: 520
    property bool copiedRecently: false

    visible: player.statsVisible
    width: Math.min(430, parent ? Math.max(300, parent.width - 40) : 430)
    height: Math.min(maxHeight, contentCol.implicitHeight + 24)
    radius: Theme.radiusMd
    color: Qt.rgba(0.05, 0.06, 0.09, 0.88)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.16)
    clip: true

    readonly property var statsMap: player.stats || ({})

    function pick(keys, fallback) {
        var s = statsMap
        if (!s)
            return fallback
        for (var i = 0; i < keys.length; ++i) {
            var k = keys[i]
            if (s[k] !== undefined && s[k] !== null && String(s[k]).length > 0)
                return String(s[k])
        }
        return fallback
    }

    function resolutionText() {
        var res = pick(["resolution", "videoResolution", "video_resolution"], "")
        if (res.length > 0)
            return res
        var w = pick(["videoWidth", "width", "video_width"], "")
        var h = pick(["videoHeight", "height", "video_height"], "")
        if (w.length > 0 && h.length > 0)
            return w + "×" + h
        return "—"
    }

    function videoSummary() {
        var parts = []
        var codec = pick(["codec", "videoCodec", "video_codec", "vcodec"], "")
        if (codec.length > 0)
            parts.push(codec.toUpperCase())
        var res = resolutionText()
        if (res !== "—")
            parts.push(res)
        var fps = pick(["estimatedFps", "fps", "videoFps", "estimatedVfFps", "containerFps", "video_fps"], "")
        if (fps.length > 0)
            parts.push(fps + (fps.indexOf("fps") === -1 ? " fps" : ""))
        return parts.length > 0 ? parts.join(" · ") : "—"
    }

    function audioSummary() {
        var parts = []
        var codec = pick(["audioCodec", "audio_codec", "acodec"], "")
        if (codec.length > 0)
            parts.push(codec.toUpperCase())
        var ch = pick(["audioChannels", "audio_channels", "channels"], "")
        if (ch.length > 0)
            parts.push(ch)
        var sr = pick(["audioSampleRate", "audio_sample_rate", "sampleRate"], "")
        if (sr.length > 0)
            parts.push(sr)
        return parts.length > 0 ? parts.join(" · ") : "—"
    }

    function logLines() {
        var s = statsMap
        if (!s)
            return []
        var arr = s.logs || s.recentLogs || s.recent_logs || []
        return Array.isArray(arr) ? arr : []
    }

    Timer {
        id: copiedResetTimer
        interval: 1600
        repeat: false
        onTriggered: root.copiedRecently = false
    }

    // Consume clicks inside the overlay
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
    }

    component StatRow: RowLayout {
        property string keyLabel: ""
        property string valText: "—"
        property color valColor: "#E8ECF5"

        Layout.fillWidth: true
        spacing: 10

        Text {
            text: keyLabel
            color: Qt.rgba(1, 1, 1, 0.54)
            font.family: Theme.fontMono
            font.pixelSize: 11
            Layout.preferredWidth: 112
            elide: Text.ElideRight
        }

        Text {
            text: valText
            color: valColor
            font.family: Theme.fontMono
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    component GroupTitle: Text {
        color: Theme.accent
        font.family: Theme.fontMono
        font.pixelSize: 10
        font.weight: Font.Bold
        font.letterSpacing: 0.9
        Layout.fillWidth: true
        Layout.topMargin: 6
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: width
        contentHeight: contentCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        ColumnLayout {
            id: contentCol
            width: parent.width
            spacing: 4

            // Header bar
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "PLAYBACK STATS"
                    color: "#FFFFFF"
                    font.family: Theme.fontMono
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    font.letterSpacing: 0.6
                    Layout.fillWidth: true
                }

                Rectangle {
                    implicitWidth: copyText.implicitWidth + 16
                    implicitHeight: 24
                    radius: Theme.radiusSm
                    color: copyArea.containsMouse
                        ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
                        : Qt.rgba(1, 1, 1, 0.10)
                    border.width: 1
                    border.color: root.copiedRecently ? Theme.success : Qt.rgba(1, 1, 1, 0.18)

                    Text {
                        id: copyText
                        anchors.centerIn: parent
                        text: root.copiedRecently ? "Copied!" : "Copy Diagnostics"
                        color: root.copiedRecently ? Theme.success : "#F4F6FA"
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        id: copyArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            player.copyDiagnosticsToClipboard()
                            root.copiedRecently = true
                            copiedResetTimer.restart()
                        }
                    }
                }

                Rectangle {
                    width: 24
                    height: 24
                    radius: 12
                    color: closeStatsArea.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: Qt.rgba(1, 1, 1, 0.78)
                        font.pixelSize: 16
                    }

                    MouseArea {
                        id: closeStatsArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: player.statsVisible = false
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Layout.bottomMargin: 2
                height: 1
                color: Qt.rgba(1, 1, 1, 0.12)
            }

            // Jellyfin Stream Info
            GroupTitle { text: "JELLYFIN STREAM" }
            StatRow {
                keyLabel: "Method"
                valText: player.transcoding ? "Transcoding" : "Direct Play"
                valColor: player.transcoding ? Theme.warning : Theme.success
            }
            StatRow {
                visible: player.transcoding && player.transcodeReasons.length > 0
                keyLabel: "Reason"
                valText: player.transcodeReasons.join(", ")
                valColor: Theme.warning
            }
            StatRow {
                keyLabel: "Container"
                valText: root.pick(["container", "fileFormat", "format"], "—")
            }
            StatRow {
                keyLabel: "Bitrate / Size"
                valText: {
                    var br = root.pick(["sourceBitrate", "totalBitrate", "bitrate", "streamBitrate"], "")
                    var sz = root.pick(["fileSize", "file_size", "size"], "")
                    if (br === "—") br = ""
                    if (sz === "—") sz = ""
                    if (br.length > 0 && sz.length > 0) return br + " · " + sz
                    return br.length > 0 ? br : (sz.length > 0 ? sz : "—")
                }
            }

            // Video
            GroupTitle { text: "VIDEO" }
            StatRow {
                keyLabel: "Stream"
                valText: root.videoSummary()
            }
            StatRow {
                keyLabel: "HW Decoder"
                valText: root.pick(["hwdec", "hwdecCurrent", "hwdec_current", "hardwareDecoder"], "—")
            }
            StatRow {
                keyLabel: "Pixel / Color"
                valText: {
                    var pf = root.pick(["pixelFormat", "pixel_format", "pixfmt"], "")
                    var cs = root.pick(["colorSpace", "color_space", "colormatrix", "hdr"], "")
                    if (pf.length > 0 && cs.length > 0) return pf + " · " + cs
                    return pf.length > 0 ? pf : (cs.length > 0 ? cs : "—")
                }
            }
            StatRow {
                keyLabel: "Video Bitrate"
                valText: root.pick(["videoBitrate", "video_bitrate"], "—")
            }

            // Audio
            GroupTitle { text: "AUDIO" }
            StatRow {
                keyLabel: "Stream"
                valText: root.audioSummary()
            }
            StatRow {
                keyLabel: "Audio Bitrate"
                valText: root.pick(["audioBitrate", "audio_bitrate"], "—")
            }

            // Performance & Cache
            GroupTitle { text: "PERFORMANCE & CACHE" }
            StatRow {
                keyLabel: "Dropped Frames"
                valText: root.pick(["droppedFrames", "dropped_frames", "frameDropCount"], "0")
            }
            StatRow {
                keyLabel: "A/V Sync"
                valText: root.pick(["avSync", "avsync", "av_sync"], "—")
            }
            StatRow {
                keyLabel: "Cache"
                valText: {
                    var dur = root.pick(["cacheDuration", "cache_duration"], "")
                    if (dur.length === 0 && root.videoItem)
                        dur = root.videoItem.cacheDuration.toFixed(1) + " s"
                    var sp = root.pick(["cacheSpeed", "cache_speed", "cacheSize", "cacheUsed"], "")
                    return sp.length > 0 ? (dur + " · " + sp) : (dur.length > 0 ? dur : "—")
                }
            }
            StatRow {
                keyLabel: "Playback Speed"
                valText: root.videoItem ? (root.videoItem.speed.toFixed(2) + "×") : "1.00×"
            }

            // Recent mpv logs
            GroupTitle {
                text: "RECENT MPV LOGS"
                visible: root.logLines().length > 0
            }

            Rectangle {
                visible: root.logLines().length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(110, logText.implicitHeight + 12)
                radius: Theme.radiusSm
                color: Qt.rgba(0, 0, 0, 0.45)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.08)
                clip: true

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 6
                    contentWidth: width
                    contentHeight: logText.implicitHeight
                    clip: true

                    Text {
                        id: logText
                        width: parent.width
                        text: root.logLines().slice(-12).join("\n")
                        color: Qt.rgba(1, 1, 1, 0.72)
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
