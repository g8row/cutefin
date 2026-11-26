import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root

    property bool open: false
    property string activeTab: "tracks" // "tracks", "style", "audio", "chapters", "quality"
    property var videoItem: null

    signal tabSelected(string tabName)
    signal closeRequested()

    width: 360
    color: Theme.bgSurface
    border.width: 1
    border.color: Theme.borderStrong
    visible: open || slideBehaviorRunning
    anchors.rightMargin: open ? 0 : -width

    property bool slideBehaviorRunning: false

    Behavior on anchors.rightMargin {
        enabled: !app.reduceMotion
        NumberAnimation {
            duration: Theme.animNormal
            easing.type: Easing.OutCubic
            onRunningChanged: root.slideBehaviorRunning = running
        }
    }

    // Helper: format mpv track variant nicely
    function trackTitle(track) {
        if (!track)
            return "Unknown"
        if (track.label && String(track.label).length > 0)
            return String(track.label)
        var parts = []
        if (track.title && String(track.title).length > 0)
            parts.push(String(track.title))
        if (track.lang && String(track.lang).length > 0) {
            var langUpper = String(track.lang).toUpperCase()
            if (parts.length === 0 || parts[0].toUpperCase().indexOf(langUpper) === -1)
                parts.push(langUpper)
        }
        if (parts.length === 0)
            parts.push("Track " + (track.id !== undefined ? track.id : ""))
        return parts.join(" · ")
    }

    function trackMeta(track) {
        if (!track)
            return ""
        var tags = []
        if (track.codec && String(track.codec).length > 0)
            tags.push(String(track.codec).toUpperCase())
        if (track.forced)
            tags.push("Forced")
        if (track.hearingImpaired)
            tags.push("SDH")
        if (track.external)
            tags.push("External")
        return tags.join(" · ")
    }

    function chapterStartSec(ch) {
        if (!ch)
            return 0
        if (ch.startSeconds !== undefined)
            return Number(ch.startSeconds)
        if (ch.startTicks !== undefined)
            return Number(ch.startTicks) / 10000000.0
        return 0
    }

    readonly property int currentChapterIndex: {
        if (!root.open || root.activeTab !== "chapters" || !root.videoItem)
            return -1
        var list = player.chapters
        if (!list || list.length === 0)
            return -1
        var pos = root.videoItem.position
        var current = -1
        for (var i = 0; i < list.length; ++i) {
            var start = root.chapterStartSec(list[i])
            if (pos >= start - 0.25)
                current = i
            else
                break
        }
        return current
    }

    function applySubSettings() {
        player.applyPresentationSettings()
    }

    // Prevent clicks on drawer background from reaching the video surface
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function(wheel) { wheel.accepted = false }
    }

    // Reusable UI building blocks inside the drawer
    component SectionHeader: Text {
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: 11
        font.weight: Font.Bold
        font.letterSpacing: 0.8
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Layout.topMargin: 10
        Layout.bottomMargin: 4
    }

    component OptionRow: Rectangle {
        id: optRoot
        property string label: ""
        property string sublabel: ""
        property bool selected: false
        signal clicked()

        Layout.fillWidth: true
        implicitHeight: sublabel.length > 0 ? 50 : 38
        radius: Theme.radiusMd
        color: optArea.pressed
            ? Theme.bgCardHover
            : (selected
                ? Theme.accentMuted
                : (optArea.containsMouse ? Theme.bgCardHover : Theme.bgCard))
        border.width: 1
        border.color: selected ? Theme.accent : Theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: optRoot.label
                    color: optRoot.selected ? Theme.textPrimary : Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: optRoot.selected ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    visible: optRoot.sublabel.length > 0
                    text: optRoot.sublabel
                    color: Theme.textMuted
                    font.family: Theme.fontMono
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                visible: optRoot.selected
                width: 8
                height: 8
                radius: 4
                color: Theme.accent
            }
        }

        MouseArea {
            id: optArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: optRoot.clicked()
        }
    }

    component StepperChip: Rectangle {
        id: chipRoot
        property string label: ""
        property bool accentTone: false
        signal clicked()

        implicitWidth: Math.max(36, chipLbl.implicitWidth + 16)
        implicitHeight: 30
        radius: Theme.radiusSm
        color: chipArea.pressed
            ? Theme.bgCardHover
            : (accentTone
                ? Theme.accentMuted
                : (chipArea.containsMouse ? Theme.bgCardHover : Theme.bgElevated))
        border.width: 1
        border.color: accentTone ? Theme.accent : Theme.border

        Text {
            id: chipLbl
            anchors.centerIn: parent
            text: chipRoot.label
            color: accentTone ? Theme.accent : Theme.textPrimary
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontCaption
            font.weight: Font.DemiBold
        }

        MouseArea {
            id: chipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chipRoot.clicked()
        }
    }

    component ToggleRow: Rectangle {
        id: togRoot
        property string label: ""
        property string description: ""
        property bool checked: false
        signal toggled(bool newValue)

        Layout.fillWidth: true
        implicitHeight: description.length > 0 ? 54 : 42
        radius: Theme.radiusMd
        color: Theme.bgCard
        border.width: 1
        border.color: Theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: togRoot.label
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    visible: togRoot.description.length > 0
                    text: togRoot.description
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                width: 40
                height: 22
                radius: 11
                color: togRoot.checked ? Theme.accent : Theme.bgInput
                border.width: 1
                border.color: togRoot.checked ? Theme.accent : Theme.borderStrong

                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    anchors.verticalCenter: parent.verticalCenter
                    x: togRoot.checked ? (parent.width - width - 3) : 3
                    color: "#FFFFFF"

                    Behavior on x {
                        enabled: !app.reduceMotion
                        NumberAnimation { duration: Theme.animFast }
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: togRoot.toggled(!togRoot.checked)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // -------------------------------------------------------------------
        // Drawer Header
        // -------------------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 54

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    text: {
                        switch (root.activeTab) {
                        case "tracks": return "Tracks"
                        case "style": return "Subtitles"
                        case "audio": return "Audio"
                        case "chapters": return "Chapters"
                        case "quality": return "Quality & Speed"
                        default: return "Playback"
                        }
                    }
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontH2
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                Rectangle {
                    width: 30
                    height: 30
                    radius: 15
                    color: closeArea.containsMouse ? Theme.bgCardHover : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: Theme.textSecondary
                        font.pixelSize: 20
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeRequested()
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }

        // -------------------------------------------------------------------
        // Tab Switcher
        // -------------------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 42

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.topMargin: 6
                anchors.bottomMargin: 6
                spacing: 4

                Repeater {
                    model: [
                        { id: "tracks", label: "Tracks" },
                        { id: "style", label: "Subtitles" },
                        { id: "audio", label: "Audio" },
                        { id: "chapters", label: "Chapters" },
                        { id: "quality", label: "Quality" }
                    ]

                    delegate: Rectangle {
                        readonly property bool isCurrent: root.activeTab === modelData.id
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.radiusSm
                        color: isCurrent
                            ? Theme.accentMuted
                            : (tabArea.containsMouse ? Theme.bgCardHover : "transparent")
                        border.width: isCurrent ? 1 : 0
                        border.color: isCurrent ? Theme.accent : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: isCurrent ? Theme.accent : Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: isCurrent ? Font.DemiBold : Font.Medium
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            id: tabArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.tabSelected(modelData.id)
                        }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }

        // -------------------------------------------------------------------
        // Scrollable Tab Content
        // -------------------------------------------------------------------
        Flickable {
            id: drawerFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: tabContentCol.implicitHeight + 32
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            ColumnLayout {
                id: tabContentCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 6

                Loader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? item.implicitHeight : 0
                    active: root.open && root.activeTab === "tracks"
                    visible: active
                    sourceComponent: tracksTabComp
                }

                Loader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? item.implicitHeight : 0
                    active: root.open && root.activeTab === "style"
                    visible: active
                    sourceComponent: styleTabComp
                }

                Loader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? item.implicitHeight : 0
                    active: root.open && root.activeTab === "audio"
                    visible: active
                    sourceComponent: audioTabComp
                }

                Loader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? item.implicitHeight : 0
                    active: root.open && root.activeTab === "chapters"
                    visible: active
                    sourceComponent: chaptersTabComp
                }

                Loader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? item.implicitHeight : 0
                    active: root.open && root.activeTab === "quality"
                    visible: active
                    sourceComponent: qualityTabComp
                }
            }
        }
    }

    // =======================================================================
    // TAB 1: TRACKS (Audio, Primary Subtitles, Secondary Subtitles)
    // =======================================================================
    Component {
        id: tracksTabComp
        ColumnLayout {
            spacing: 6

            SectionHeader { text: "AUDIO TRACKS" }

            Text {
                visible: !root.videoItem || root.videoItem.audioTracks.length === 0
                text: "No audio tracks detected"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                Layout.leftMargin: 4
            }

            Repeater {
                model: root.videoItem ? root.videoItem.audioTracks : []
                delegate: OptionRow {
                    label: root.trackTitle(modelData)
                    sublabel: root.trackMeta(modelData)
                    selected: root.videoItem && modelData.id === root.videoItem.selectedAudioId
                    onClicked: player.selectAudioTrack(modelData.id)
                }
            }

            SectionHeader { text: "SUBTITLES" }

            OptionRow {
                label: "Off"
                selected: !root.videoItem || root.videoItem.selectedSubtitleId <= 0
                onClicked: player.selectSubtitleTrack(-1)
            }

            Repeater {
                model: root.videoItem ? root.videoItem.subtitleTracks : []
                delegate: OptionRow {
                    label: root.trackTitle(modelData)
                    sublabel: root.trackMeta(modelData)
                    selected: root.videoItem && modelData.id === root.videoItem.selectedSubtitleId
                    onClicked: player.selectSubtitleTrack(modelData.id)
                }
            }

            SectionHeader {
                text: "SECONDARY SUBTITLES"
                visible: root.videoItem && root.videoItem.subtitleTracks.length > 0
            }

            OptionRow {
                visible: root.videoItem && root.videoItem.subtitleTracks.length > 0
                label: "Off"
                selected: !root.videoItem || root.videoItem.selectedSecondarySubtitleId <= 0
                onClicked: player.selectSecondarySubtitleTrack(-1)
            }

            Repeater {
                model: root.videoItem ? root.videoItem.subtitleTracks : []
                delegate: OptionRow {
                    label: root.trackTitle(modelData)
                    sublabel: root.trackMeta(modelData)
                    selected: root.videoItem && modelData.id === root.videoItem.selectedSecondarySubtitleId
                    onClicked: player.selectSecondarySubtitleTrack(modelData.id)
                }
            }
        }
    }

    // =======================================================================
    // TAB 2: SUBTITLES (Delay + Live Appearance Settings)
    // =======================================================================
    Component {
        id: styleTabComp
        ColumnLayout {
            spacing: 8

            SectionHeader { text: "SUBTITLE SYNC DELAY" }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 74
                radius: Theme.radiusMd
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Delay (Z / X)"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                        }
                        Text {
                            text: (player.subtitleDelay >= 0 ? "+" : "") + player.subtitleDelay.toFixed(1) + " s"
                            color: Math.abs(player.subtitleDelay) > 0.01 ? Theme.accent : Theme.textPrimary
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        StepperChip { label: "-0.5s"; onClicked: player.adjustSubtitleDelay(-0.5) }
                        StepperChip { label: "-0.1s"; onClicked: player.adjustSubtitleDelay(-0.1) }
                        StepperChip {
                            label: "0.0s"
                            accentTone: Math.abs(player.subtitleDelay) > 0.01
                            Layout.fillWidth: true
                            onClicked: player.adjustSubtitleDelay(-player.subtitleDelay)
                        }
                        StepperChip { label: "+0.1s"; onClicked: player.adjustSubtitleDelay(0.1) }
                        StepperChip { label: "+0.5s"; onClicked: player.adjustSubtitleDelay(0.5) }
                    }
                }
            }

            SectionHeader { text: "APPEARANCE" }

            // Font Size
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 54
                radius: Theme.radiusMd
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 10
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            text: "Font Size"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                        }
                        Text {
                            text: app.subtitleFontSize + " pt"
                            color: Theme.textMuted
                            font.family: Theme.fontMono
                            font.pixelSize: 11
                        }
                    }

                    StepperChip {
                        label: "−"
                        onClicked: {
                            app.subtitleFontSize = Math.max(20, app.subtitleFontSize - 4)
                            root.applySubSettings()
                        }
                    }
                    StepperChip {
                        label: "+"
                        onClicked: {
                            app.subtitleFontSize = Math.min(96, app.subtitleFontSize + 4)
                            root.applySubSettings()
                        }
                    }
                }
            }

            // Outline Size
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 54
                radius: Theme.radiusMd
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 10
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            text: "Outline Thickness"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                        }
                        Text {
                            text: app.subtitleOutlineSize.toFixed(1)
                            color: Theme.textMuted
                            font.family: Theme.fontMono
                            font.pixelSize: 11
                        }
                    }

                    StepperChip {
                        label: "−"
                        onClicked: {
                            app.subtitleOutlineSize = Math.max(0.0, Math.round((app.subtitleOutlineSize - 0.5) * 10) / 10)
                            root.applySubSettings()
                        }
                    }
                    StepperChip {
                        label: "+"
                        onClicked: {
                            app.subtitleOutlineSize = Math.min(6.0, Math.round((app.subtitleOutlineSize + 0.5) * 10) / 10)
                            root.applySubSettings()
                        }
                    }
                }
            }

            // Vertical Position
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 54
                radius: Theme.radiusMd
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 10
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            text: "Vertical Position"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                        }
                        Text {
                            text: app.subtitlePositionPercent + "% (100% = bottom)"
                            color: Theme.textMuted
                            font.family: Theme.fontMono
                            font.pixelSize: 11
                        }
                    }

                    StepperChip {
                        label: "Up"
                        onClicked: {
                            app.subtitlePositionPercent = Math.max(10, app.subtitlePositionPercent - 5)
                            root.applySubSettings()
                        }
                    }
                    StepperChip {
                        label: "Down"
                        onClicked: {
                            app.subtitlePositionPercent = Math.min(150, app.subtitlePositionPercent + 5)
                            root.applySubSettings()
                        }
                    }
                }
            }

            // Text Color Swatches
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 54
                radius: Theme.radiusMd
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    Text {
                        text: "Text Color"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        Layout.fillWidth: true
                    }

                    Repeater {
                        model: [
                            { hex: "#FFFFFFFF", swatch: "#FFFFFF" },
                            { hex: "#FFFFEA61", swatch: "#FFEA61" },
                            { hex: "#FF7DE3FF", swatch: "#7DE3FF" },
                            { hex: "#FFD0D5E0", swatch: "#D0D5E0" }
                        ]
                        delegate: Rectangle {
                            readonly property bool active: app.subtitleColor.toUpperCase() === modelData.hex
                            width: 24
                            height: 24
                            radius: 12
                            color: modelData.swatch
                            border.width: active ? 2 : 1
                            border.color: active ? Theme.accent : Theme.borderStrong

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    app.subtitleColor = modelData.hex
                                    root.applySubSettings()
                                }
                            }
                        }
                    }
                }
            }

            ToggleRow {
                label: "Background Box"
                description: "Draw shaded box behind subtitles"
                checked: app.subtitleBackgroundBoxEnabled
                onToggled: function(val) {
                    app.subtitleBackgroundBoxEnabled = val
                    root.applySubSettings()
                }
            }

            ToggleRow {
                label: "Override ASS Styling"
                description: "Force custom style on ASS/SSA tracks"
                checked: app.subtitleAssOverride
                onToggled: function(val) {
                    app.subtitleAssOverride = val
                    root.applySubSettings()
                }
            }
        }
    }

    // =======================================================================
    // TAB 3: AUDIO (Delay, Output Devices, Passthrough, Norm, Layout)
    // =======================================================================
    Component {
        id: audioTabComp
        ColumnLayout {
            spacing: 8

            SectionHeader { text: "AUDIO SYNC DELAY" }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 74
                radius: Theme.radiusMd
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Audio Offset"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption
                            Layout.fillWidth: true
                        }
                        Text {
                            text: (player.audioDelay >= 0 ? "+" : "") + player.audioDelay.toFixed(2) + " s"
                            color: Math.abs(player.audioDelay) > 0.005 ? Theme.accent : Theme.textPrimary
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        StepperChip { label: "-0.10"; onClicked: player.adjustAudioDelay(-0.10) }
                        StepperChip { label: "-0.05"; onClicked: player.adjustAudioDelay(-0.05) }
                        StepperChip {
                            label: "0.00"
                            accentTone: Math.abs(player.audioDelay) > 0.005
                            Layout.fillWidth: true
                            onClicked: player.adjustAudioDelay(-player.audioDelay)
                        }
                        StepperChip { label: "+0.05"; onClicked: player.adjustAudioDelay(0.05) }
                        StepperChip { label: "+0.10"; onClicked: player.adjustAudioDelay(0.10) }
                    }
                }
            }

            SectionHeader { text: "PROCESSING & CHANNELS" }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 68
                radius: Theme.radiusMd
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Text {
                        text: "Channel Layout"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Repeater {
                            model: [
                                { value: "auto", label: "Auto" },
                                { value: "stereo", label: "Stereo" },
                                { value: "5.1", label: "5.1" },
                                { value: "7.1", label: "7.1" }
                            ]
                            delegate: StepperChip {
                                Layout.fillWidth: true
                                label: modelData.label
                                accentTone: app.audioChannelLayout === modelData.value
                                onClicked: {
                                    app.audioChannelLayout = modelData.value
                                    root.applySubSettings()
                                }
                            }
                        }
                    }
                }
            }

            ToggleRow {
                label: "HDMI / S/PDIF Passthrough"
                description: "Bitstream AC3, DTS, and E-AC3 when supported"
                checked: app.audioPassthroughEnabled
                onToggled: function(val) {
                    app.audioPassthroughEnabled = val
                    root.applySubSettings()
                }
            }

            ToggleRow {
                label: "Volume Normalization"
                description: "Balance quiet dialogue and loud effects"
                checked: app.audioNormalizationEnabled
                onToggled: function(val) {
                    app.audioNormalizationEnabled = val
                    root.applySubSettings()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 2

                Text {
                    text: "OUTPUT DEVICE"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                    Layout.fillWidth: true
                }

                StepperChip {
                    label: "Refresh"
                    implicitHeight: 24
                    onClicked: player.refreshAudioDevices()
                }
            }

            Repeater {
                model: player.audioDevices
                delegate: OptionRow {
                    readonly property string devName: modelData.name || "auto"
                    readonly property string devDesc: modelData.description || devName
                    label: devDesc
                    sublabel: devName !== devDesc ? devName : ""
                    selected: app.audioDevice === devName || (devName === "auto" && app.audioDevice.length === 0)
                    onClicked: {
                        app.audioDevice = devName
                        root.applySubSettings()
                    }
                }
            }
        }
    }

    // =======================================================================
    // TAB 4: CHAPTERS
    // =======================================================================
    Component {
        id: chaptersTabComp
        ColumnLayout {
            spacing: 6

            SectionHeader { text: "CHAPTERS" }

            Text {
                visible: player.chapters.length === 0
                text: "This title has no chapter markers."
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                Layout.leftMargin: 4
            }

            Repeater {
                model: player.chapters
                delegate: OptionRow {
                    readonly property double startSec: root.chapterStartSec(modelData)
                    label: (index + 1) + ".  " + (modelData.name || ("Chapter " + (index + 1)))
                    sublabel: Theme.formatTime(startSec)
                    selected: index === root.currentChapterIndex
                    onClicked: player.seekToChapter(index)
                }
            }
        }
    }

    // =======================================================================
    // TAB 5: QUALITY & PLAYBACK SPEED
    // =======================================================================
    Component {
        id: qualityTabComp
        ColumnLayout {
            spacing: 8

            // Direct Play / Transcoding Status Banner
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: statusCol.implicitHeight + 20
                radius: Theme.radiusMd
                color: player.transcoding
                    ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.12)
                    : Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.12)
                border.width: 1
                border.color: player.transcoding
                    ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.50)
                    : Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.50)

                ColumnLayout {
                    id: statusCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: player.transcoding ? Theme.warning : Theme.success
                        }

                        Text {
                            text: player.transcoding ? "Transcoding" : "Direct Play"
                            color: player.transcoding ? Theme.warning : Theme.success
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        visible: player.transcoding && player.transcodeReasons.length > 0
                        text: "Reason: " + player.transcodeReasons.join(", ")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            SectionHeader { text: "PLAYBACK SPEED" }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                rowSpacing: 6
                columnSpacing: 6

                Repeater {
                    model: [
                        { val: 0.5, label: "0.5×" },
                        { val: 0.75, label: "0.75×" },
                        { val: 1.0, label: "1×" },
                        { val: 1.25, label: "1.25×" },
                        { val: 1.5, label: "1.5×" },
                        { val: 2.0, label: "2×" }
                    ]
                    delegate: StepperChip {
                        Layout.fillWidth: true
                        implicitHeight: 34
                        label: modelData.label
                        accentTone: root.videoItem && Math.abs(root.videoItem.speed - modelData.val) < 0.01
                        onClicked: {
                            if (root.videoItem)
                                root.videoItem.setSpeed(modelData.val)
                        }
                    }
                }
            }

            SectionHeader { text: "MAX STREAM BITRATE" }

            Repeater {
                model: [
                    { mbps: 0, label: "Auto / Direct Play", sub: "Original quality without transcoding" },
                    { mbps: 40, label: "40 Mbps", sub: "4K Ultra HD" },
                    { mbps: 20, label: "20 Mbps", sub: "1080p High" },
                    { mbps: 10, label: "10 Mbps", sub: "1080p Standard" },
                    { mbps: 6, label: "6 Mbps", sub: "720p High" },
                    { mbps: 3, label: "3 Mbps", sub: "720p Standard" },
                    { mbps: 1, label: "1.5 Mbps", sub: "480p Bandwidth Saver" }
                ]
                delegate: OptionRow {
                    label: modelData.label
                    sublabel: modelData.sub
                    selected: player.maxBitrateMbps === modelData.mbps
                    onClicked: player.setMaxBitrateMbps(modelData.mbps)
                }
            }
        }
    }
}
