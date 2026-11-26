import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root
    color: Theme.bgCanvas

    // ------------------------------------------------------------------
    // Inline building blocks
    // ------------------------------------------------------------------

    component SettingRow : RowLayout {
        id: row
        property string title: ""
        property string description: ""
        default property alias content: contentSlot.children

        Layout.fillWidth: true
        spacing: Theme.spaceMd

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: row.title
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: row.description.length > 0
                text: row.description
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
        Item {
            id: contentSlot
            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }

    component DebouncedTextField : AppTextField {
        id: debouncedField
        property string committedValue: ""
        signal valueCommitted(string value)

        width: 180
        text: committedValue

        function flush() {
            debounceTimer.stop()
            if (text !== committedValue)
                valueCommitted(text)
        }

        Timer {
            id: debounceTimer
            interval: 350
            repeat: false
            onTriggered: {
                if (debouncedField.text !== debouncedField.committedValue)
                    debouncedField.valueCommitted(debouncedField.text)
            }
        }

        onTextChanged: {
            if (text !== committedValue)
                debounceTimer.restart()
            else
                debounceTimer.stop()
        }
        onAccepted: flush()
        Component.onDestruction: {
            if (debounceTimer.running)
                flush()
        }
    }

    component SettingSwitch : Item {
        id: sw
        property bool checked: false
        signal toggled(bool checked)

        implicitWidth: 44
        implicitHeight: 24
        width: 44
        height: 24

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: sw.checked ? Theme.accent : Theme.bgInput
            border.width: 1
            border.color: sw.checked ? Theme.accent : Theme.borderStrong
            Rectangle {
                width: 18
                height: 18
                radius: 9
                y: 3
                x: sw.checked ? parent.width - width - 3 : 3
                color: "#FFFFFF"
                Behavior on x {
                    NumberAnimation { duration: Theme.animFast }
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: sw.toggled(!sw.checked)
        }
    }

    component SettingCombo : ComboBox {
        id: combo
        implicitWidth: 210
        textRole: "label"
        valueRole: "value"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall

        background: Rectangle {
            color: Theme.bgInput
            radius: Theme.radiusMd
            border.width: 1
            border.color: (combo.hovered || combo.activeFocus) ? Theme.borderStrong : Theme.border
        }
        contentItem: Text {
            leftPadding: Theme.spaceSm
            rightPadding: Theme.spaceLg
            text: combo.displayText
            color: Theme.textPrimary
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: AppIcon {
            name: "chevron-right"
            rotation: 90
            size: 12
            color: Theme.textMuted
            anchors.right: parent.right
            anchors.rightMargin: Theme.spaceSm
            anchors.verticalCenter: parent.verticalCenter
        }
        popup.background: Rectangle {
            implicitWidth: combo.width
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.borderStrong
            radius: Theme.radiusMd
        }
        delegate: ItemDelegate {
            id: comboDelegate
            required property int index
            width: combo.width
            highlighted: combo.highlightedIndex === index
            background: Rectangle {
                color: comboDelegate.highlighted ? Theme.bgCardHover : "transparent"
                radius: Theme.radiusSm
            }
            contentItem: Text {
                text: combo.textAt(comboDelegate.index)
                color: comboDelegate.highlighted ? Theme.textPrimary : Theme.textSecondary
                font: combo.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    component SettingSlider : Slider {
        id: slider
        implicitWidth: 170

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Theme.bgInput
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.accent
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            color: Theme.textPrimary
            border.width: 2
            border.color: Theme.accent
        }
    }

    component SwatchRow : Row {
        id: swatchRow
        property var colors: []
        property string selected: ""
        signal picked(string value)

        spacing: Theme.spaceSm
        Repeater {
            model: swatchRow.colors
            delegate: Rectangle {
                width: 24
                height: 24
                radius: 12
                color: modelData
                border.width: 2
                border.color: String(modelData).toLowerCase() === String(swatchRow.selected).toLowerCase()
                    ? Theme.textPrimary
                    : Theme.borderStrong
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: swatchRow.picked(String(modelData))
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Data
    // ------------------------------------------------------------------

    readonly property var sectionNames: [
        "Appearance", "Playback", "Subtitles", "Audio", "Account & Storage", "Advanced"
    ]
    property int activeSection: 0

    readonly property var accentChoices: ["#AA5CC3", "#00A4DC", "#8B5CF6", "#10B981", "#F59E0B", "#F43F5E"]

    readonly property var themeModel: [
        { label: "Dark", value: "dark" },
        { label: "Light", value: "light" },
        { label: "System", value: "system" }
    ]
    readonly property var hwdecModel: [
        { label: "Auto (safe)", value: "auto-safe" },
        { label: "Disabled", value: "no" },
        { label: "VideoToolbox (macOS)", value: "videotoolbox" },
        { label: "VAAPI (Linux)", value: "vaapi" },
        { label: "NVIDIA NVDEC", value: "nvdec" },
        { label: "Direct3D 11VA (Windows)", value: "d3d11va" }
    ]
    readonly property var profileModel: [
        { label: "Fast", value: "fast" },
        { label: "Balanced", value: "balanced" },
        { label: "High quality", value: "high-quality" }
    ]
    readonly property var bitrateModel: [
        { label: "Auto / Direct Play", value: 0 },
        { label: "120 Mbps", value: 120 },
        { label: "40 Mbps", value: 40 },
        { label: "20 Mbps", value: 20 },
        { label: "10 Mbps", value: 10 },
        { label: "6 Mbps", value: 6 },
        { label: "3 Mbps", value: 3 }
    ]
    readonly property var channelModel: [
        { label: "Auto", value: "auto" },
        { label: "Stereo", value: "stereo" },
        { label: "5.1", value: "5.1" },
        { label: "7.1", value: "7.1" }
    ]
    readonly property var logLevelModel: [
        { label: "No output", value: "no" },
        { label: "Fatal", value: "fatal" },
        { label: "Error", value: "error" },
        { label: "Warning", value: "warn" },
        { label: "Info", value: "info" },
        { label: "Status", value: "status" },
        { label: "Verbose", value: "v" },
        { label: "Debug", value: "debug" },
        { label: "Trace", value: "trace" }
    ]

    readonly property var textColorChoices: ["#FFFFFFFF", "#FF000000", "#FFFFFF00", "#FFFF0000", "#FF00FF00", "#FF0000FF", "#FF00FFFF", "#FF808080"]
    readonly property var outlineColorChoices: ["#FF000000", "#FFFFFFFF", "#FFFFFF00", "#FF808080"]
    readonly property var boxColorChoices: ["#00000000", "#80000000", "#FF000000", "#CC1A1A1A"]

    readonly property var deviceModel: {
        const devs = player.audioDevices || []
        let arr = devs.map(function(dev) {
            if (typeof dev === "string")
                return { label: dev, value: dev }
            const value = dev.name || dev.id || dev.value || ""
            const label = dev.description || dev.label || value
            return { label: String(label || value), value: String(value) }
        })
        const current = app.audioDevice
        if (current.length > 0 && !arr.some(function(d) { return d.value === current }))
            arr.unshift({ label: current + " (current)", value: current })
        if (arr.length === 0)
            arr.push({ label: "Auto", value: "auto" })
        return arr
    }

    property string mpvOptionsText: ""

    Component.onCompleted: {
        mpvOptionsText = app.customMpvOptions
        player.refreshAudioDevices()
        app.refreshCacheSize()
    }

    function scrollToSection(index) {
        const items = [secAppearance, secPlayback, secSubtitles, secAudio, secAccount, secAdvanced]
        if (index < 0 || index >= items.length)
            return
        scrollAnim.to = Math.max(0, items[index].y + contentCol.y - Theme.spaceSm)
        scrollAnim.start()
    }

    NumberAnimation {
        id: scrollAnim
        target: settingsFlick
        property: "contentY"
        duration: Theme.animNormal > 0 ? Theme.animNormal : 1
        easing.type: Easing.OutCubic
    }

    // ------------------------------------------------------------------
    // Layout: left nav + right scrolling content
    // ------------------------------------------------------------------

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 216
            color: Theme.bgSurface

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spaceMd
                spacing: Theme.spaceSm

                Text {
                    text: "Settings"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontH1
                    font.weight: Font.DemiBold
                    Layout.bottomMargin: Theme.spaceSm
                }

                ListView {
                    id: navList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.sectionNames
                    currentIndex: root.activeSection
                    spacing: 4
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        id: navItem
                        required property int index
                        required property string modelData

                        width: navList.width
                        height: 40
                        radius: Theme.radiusMd
                        color: navList.currentIndex === index ? Theme.accentMuted : "transparent"

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spaceMd
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spaceSm
                            text: navItem.modelData
                            color: navList.currentIndex === index ? Theme.textPrimary : Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: navList.currentIndex === index ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.activeSection = navItem.index
                                root.scrollToSection(navItem.index)
                            }
                        }
                    }
                }
            }
        }

        Flickable {
            id: settingsFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: contentCol.implicitHeight + Theme.spaceXl * 2
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

            onContentYChanged: {
                const items = [secAppearance, secPlayback, secSubtitles, secAudio, secAccount, secAdvanced]
                let best = 0
                for (let i = 0; i < items.length; ++i) {
                    if (items[i].y + contentCol.y - Theme.spaceLg <= contentY + 4)
                        best = i
                }
                if (best !== root.activeSection)
                    root.activeSection = best
            }

            ColumnLayout {
                id: contentCol
                x: Theme.spaceXl
                y: Theme.spaceXl
                width: settingsFlick.width - Theme.spaceXl * 2
                spacing: Theme.spaceXl

                // ================= Appearance & Performance =================
                ColumnLayout {
                    id: secAppearance
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: "Appearance & Performance"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontH2
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: appearanceContent.implicitHeight + Theme.spaceLg * 2
                        color: Theme.bgSurface
                        radius: Theme.radiusLg
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: appearanceContent
                            x: Theme.spaceLg
                            y: Theme.spaceLg
                            width: parent.width - Theme.spaceLg * 2
                            height: implicitHeight
                            spacing: Theme.spaceLg

                            SettingRow {
                                title: "Theme"
                                description: "Color scheme for the whole interface."
                                SettingCombo {
                                    model: root.themeModel
                                    currentIndex: Math.max(0, indexOfValue(app.themeMode))
                                    onActivated: app.themeMode = currentValue
                                }
                            }
                            SettingRow {
                                title: "Accent color"
                                description: "Brand accent used across buttons, highlights and focus rings."
                                RowLayout {
                                    spacing: Theme.spaceSm
                                    SwatchRow {
                                        colors: root.accentChoices
                                        selected: app.accentColor
                                        onPicked: function(value) { app.accentColor = value }
                                    }
                                    Text {
                                        text: app.accentColor
                                        color: Theme.textMuted
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fontCaption
                                    }
                                }
                            }
                            SettingRow {
                                title: "Reduce Motion"
                                description: "Disables animations and transitions throughout the app."
                                SettingSwitch {
                                    checked: app.reduceMotion
                                    onToggled: function(value) { app.reduceMotion = value }
                                }
                            }
                            SettingRow {
                                title: "Dual-Core / Low-Power Mode"
                                description: app.cpuCoreCount + " CPU threads detected. Reduces animation and image effects on weak machines."
                                SettingSwitch {
                                    checked: app.dualCoreMode
                                    onToggled: function(value) { app.dualCoreMode = value }
                                }
                            }
                        }
                    }
                }

                // ================= Playback =================
                ColumnLayout {
                    id: secPlayback
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: "Playback"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontH2
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: playbackContent.implicitHeight + Theme.spaceLg * 2
                        color: Theme.bgSurface
                        radius: Theme.radiusLg
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: playbackContent
                            x: Theme.spaceLg
                            y: Theme.spaceLg
                            width: parent.width - Theme.spaceLg * 2
                            height: implicitHeight
                            spacing: Theme.spaceLg

                            SettingRow {
                                title: "Hardware decoding"
                                description: "Decoder handed to mpv. Auto (safe) picks a conservative default per platform."
                                SettingCombo {
                                    model: root.hwdecModel
                                    currentIndex: Math.max(0, indexOfValue(app.hwdecMode))
                                    onActivated: app.hwdecMode = currentValue
                                }
                            }
                            SettingRow {
                                title: "Video profile"
                                description: "Trade-off between decode speed and scaling quality."
                                SettingCombo {
                                    model: root.profileModel
                                    currentIndex: Math.max(0, indexOfValue(app.videoProfile))
                                    onActivated: app.videoProfile = currentValue
                                }
                            }
                            SettingRow {
                                title: "Max bitrate"
                                description: "Caps streaming bitrate; Auto lets the server direct play when possible."
                                SettingCombo {
                                    model: root.bitrateModel
                                    currentIndex: Math.max(0, indexOfValue(app.maxBitrateMbps))
                                    onActivated: app.maxBitrateMbps = currentValue
                                }
                            }
                            SettingRow {
                                title: "Preferred audio language"
                                description: "e.g. eng, jpn, ger. Empty means no preference."
                                DebouncedTextField {
                                    committedValue: app.preferredAudioLanguage
                                    placeholderText: "Any"
                                    onValueCommitted: function(value) { app.preferredAudioLanguage = value }
                                }
                            }
                            SettingRow {
                                title: "Preferred subtitle language"
                                description: "e.g. eng, spa. Empty means no preference."
                                DebouncedTextField {
                                    committedValue: app.preferredSubtitleLanguage
                                    placeholderText: "Any"
                                    onValueCommitted: function(value) { app.preferredSubtitleLanguage = value }
                                }
                            }
                            SettingRow {
                                title: "Auto-skip intro & credits"
                                description: "Silently seeks past detected intro/outro segments."
                                SettingSwitch {
                                    checked: app.autoSkipSegments
                                    onToggled: function(value) { app.autoSkipSegments = value }
                                }
                            }
                            SettingRow {
                                title: "Auto-play next episode"
                                description: "Starts the next episode when the countdown reaches zero."
                                SettingSwitch {
                                    checked: app.autoPlayNextEpisode
                                    onToggled: function(value) { app.autoPlayNextEpisode = value }
                                }
                            }
                        }
                    }
                }

                // ================= Subtitles =================
                ColumnLayout {
                    id: secSubtitles
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: "Subtitles"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontH2
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: subtitlesContent.implicitHeight + Theme.spaceLg * 2
                        color: Theme.bgSurface
                        radius: Theme.radiusLg
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: subtitlesContent
                            x: Theme.spaceLg
                            y: Theme.spaceLg
                            width: parent.width - Theme.spaceLg * 2
                            height: implicitHeight
                            spacing: Theme.spaceLg

                            // Live preview
                            Rectangle {
                                Layout.fillWidth: true
                                height: 150
                                radius: Theme.radiusMd
                                color: "#000000"
                                border.width: 1
                                border.color: Theme.borderStrong

                                Item {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: previewText.width + 16
                                    height: previewText.height + 8
                                    y: Math.max(6, Math.min(parent.height - height - 6,
                                        parent.height * app.subtitlePositionPercent / 100 - previewText.height / 2 - 4))

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 4
                                        visible: app.subtitleBackgroundBoxEnabled
                                        color: app.subtitleBackgroundColor
                                    }
                                    Text {
                                        id: previewText
                                        anchors.centerIn: parent
                                        text: "The quick brown fox jumps over the lazy dog"
                                        font.family: app.subtitleFontFamily.length > 0 ? app.subtitleFontFamily : Theme.fontFamily
                                        font.pixelSize: Math.max(12, Math.min(30, Math.round(app.subtitleFontSize * 0.55)))
                                        color: app.subtitleColor
                                        style: Text.Outline
                                        styleColor: app.subtitleOutlineColor
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            SettingRow {
                                title: "Font size"
                                description: "Subtitle font size in points (" + app.subtitleFontSize + ")."
                                RowLayout {
                                    spacing: Theme.spaceSm
                                    SettingSlider {
                                        from: 24
                                        to: 72
                                        stepSize: 1
                                        value: app.subtitleFontSize
                                        onMoved: app.subtitleFontSize = Math.round(value)
                                    }
                                    Text {
                                        text: String(app.subtitleFontSize)
                                        color: Theme.textMuted
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fontSmall
                                        Layout.preferredWidth: 28
                                    }
                                }
                            }
                            SettingRow {
                                title: "Font family"
                                description: "Empty uses the embedded ASS font or mpv's default."
                                DebouncedTextField {
                                    committedValue: app.subtitleFontFamily
                                    placeholderText: "Default"
                                    onValueCommitted: function(value) { app.subtitleFontFamily = value }
                                }
                            }
                            SettingRow {
                                title: "Text color"
                                SwatchRow {
                                    colors: root.textColorChoices
                                    selected: app.subtitleColor
                                    onPicked: function(value) { app.subtitleColor = value }
                                }
                            }
                            SettingRow {
                                title: "Outline color"
                                SwatchRow {
                                    colors: root.outlineColorChoices
                                    selected: app.subtitleOutlineColor
                                    onPicked: function(value) { app.subtitleOutlineColor = value }
                                }
                            }
                            SettingRow {
                                title: "Outline size"
                                description: "Outline thickness in pixels (" + app.subtitleOutlineSize.toFixed(2) + ")."
                                RowLayout {
                                    spacing: Theme.spaceSm
                                    SettingSlider {
                                        from: 0
                                        to: 6
                                        stepSize: 0.25
                                        value: app.subtitleOutlineSize
                                        onMoved: app.subtitleOutlineSize = value
                                    }
                                    Text {
                                        text: app.subtitleOutlineSize.toFixed(2)
                                        color: Theme.textMuted
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fontSmall
                                        Layout.preferredWidth: 36
                                    }
                                }
                            }
                            SettingRow {
                                title: "Shadow offset"
                                description: "Drop shadow distance in pixels (" + app.subtitleShadowOffset.toFixed(1) + ")."
                                RowLayout {
                                    spacing: Theme.spaceSm
                                    SettingSlider {
                                        from: 0
                                        to: 8
                                        stepSize: 0.5
                                        value: app.subtitleShadowOffset
                                        onMoved: app.subtitleShadowOffset = value
                                    }
                                    Text {
                                        text: app.subtitleShadowOffset.toFixed(1)
                                        color: Theme.textMuted
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fontSmall
                                        Layout.preferredWidth: 32
                                    }
                                }
                            }
                            SettingRow {
                                title: "Background box"
                                description: "Draws a filled box behind subtitle text instead of outline-only."
                                SettingSwitch {
                                    checked: app.subtitleBackgroundBoxEnabled
                                    onToggled: function(value) { app.subtitleBackgroundBoxEnabled = value }
                                }
                            }
                            SettingRow {
                                title: "Box color"
                                SwatchRow {
                                    colors: root.boxColorChoices
                                    selected: app.subtitleBackgroundColor
                                    onPicked: function(value) { app.subtitleBackgroundColor = value }
                                }
                            }
                            SettingRow {
                                title: "Vertical position"
                                description: app.subtitlePositionPercent + "% from the top (100 = bottom)."
                                RowLayout {
                                    spacing: Theme.spaceSm
                                    SettingSlider {
                                        from: 0
                                        to: 100
                                        stepSize: 5
                                        value: app.subtitlePositionPercent
                                        onMoved: app.subtitlePositionPercent = Math.round(value)
                                    }
                                    Text {
                                        text: app.subtitlePositionPercent + "%"
                                        color: Theme.textMuted
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fontSmall
                                        Layout.preferredWidth: 36
                                    }
                                }
                            }
                            SettingRow {
                                title: "Override ASS styles"
                                description: "Force these settings onto styled ASS subtitles as well."
                                SettingSwitch {
                                    checked: app.subtitleAssOverride
                                    onToggled: function(value) { app.subtitleAssOverride = value }
                                }
                            }
                        }
                    }
                }

                // ================= Audio =================
                ColumnLayout {
                    id: secAudio
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: "Audio"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontH2
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: audioContent.implicitHeight + Theme.spaceLg * 2
                        color: Theme.bgSurface
                        radius: Theme.radiusLg
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: audioContent
                            x: Theme.spaceLg
                            y: Theme.spaceLg
                            width: parent.width - Theme.spaceLg * 2
                            height: implicitHeight
                            spacing: Theme.spaceLg

                            SettingRow {
                                title: "Output device"
                                description: "Audio device handed to mpv. Refresh to re-scan."
                                RowLayout {
                                    spacing: Theme.spaceSm
                                    SettingCombo {
                                        model: root.deviceModel
                                        currentIndex: Math.max(0, indexOfValue(app.audioDevice))
                                        onActivated: app.audioDevice = currentValue
                                    }
                                    AppButton {
                                        compact: true
                                        variant: "ghost"
                                        iconName: "refresh"
                                        onClicked: player.refreshAudioDevices()
                                    }
                                }
                            }
                            SettingRow {
                                title: "Channel layout"
                                description: "Forced downmix; Auto keeps the source layout."
                                SettingCombo {
                                    model: root.channelModel
                                    currentIndex: Math.max(0, indexOfValue(app.audioChannelLayout))
                                    onActivated: app.audioChannelLayout = currentValue
                                }
                            }
                            SettingRow {
                                title: "Passthrough (AC3 / E-AC3 / DTS)"
                                description: "Sends compressed audio bitstream to the receiver when supported."
                                SettingSwitch {
                                    checked: app.audioPassthroughEnabled
                                    onToggled: function(value) { app.audioPassthroughEnabled = value }
                                }
                            }
                            SettingRow {
                                title: "Volume normalization"
                                description: "dynaudnorm filter to even out loudness between files."
                                SettingSwitch {
                                    checked: app.audioNormalizationEnabled
                                    onToggled: function(value) { app.audioNormalizationEnabled = value }
                                }
                            }
                        }
                    }
                }

                // ================= Account & Storage =================
                ColumnLayout {
                    id: secAccount
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: "Account & Storage"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontH2
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: accountContent.implicitHeight + Theme.spaceLg * 2
                        color: Theme.bgSurface
                        radius: Theme.radiusLg
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: accountContent
                            x: Theme.spaceLg
                            y: Theme.spaceLg
                            width: parent.width - Theme.spaceLg * 2
                            height: implicitHeight
                            spacing: Theme.spaceLg

                            SettingRow {
                                title: "Server"
                                Text {
                                    text: app.serverUrl
                                    color: Theme.textSecondary
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                    width: Math.min(implicitWidth, 320)
                                }
                            }
                            SettingRow {
                                title: "Server name"
                                Text {
                                    text: app.serverName + (app.serverVersion.length > 0 ? "  ·  v" + app.serverVersion : "")
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    elide: Text.ElideRight
                                }
                            }
                            SettingRow {
                                title: "Signed in as"
                                RowLayout {
                                    spacing: Theme.spaceSm
                                    AppIcon {
                                        name: "user"
                                        size: 16
                                        color: Theme.accent
                                    }
                                    Text {
                                        text: app.userName
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontBody
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            SettingRow {
                                title: "Image cache"
                                description: "Cached posters, backdrops and blurhashes on disk."
                                RowLayout {
                                    spacing: Theme.spaceSm
                                    Text {
                                        text: app.cacheSizeText
                                        color: Theme.textSecondary
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fontSmall
                                    }
                                    AppButton {
                                        compact: true
                                        variant: "secondary"
                                        iconName: "close"
                                        text: "Clear Cache"
                                        onClicked: {
                                            app.clearImageCache()
                                            app.refreshCacheSize()
                                        }
                                    }
                                }
                            }
                            SettingRow {
                                title: "Session"
                                description: "Sign out of this server on this device."
                                AppButton {
                                    variant: "danger"
                                    iconName: "logout"
                                    text: "Log Out"
                                    onClicked: app.logout()
                                }
                            }
                        }
                    }
                }

                // ================= Advanced =================
                ColumnLayout {
                    id: secAdvanced
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: "Advanced"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontH2
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: advancedContent.implicitHeight + Theme.spaceLg * 2
                        color: Theme.bgSurface
                        radius: Theme.radiusLg
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: advancedContent
                            x: Theme.spaceLg
                            y: Theme.spaceLg
                            width: parent.width - Theme.spaceLg * 2
                            height: implicitHeight
                            spacing: Theme.spaceLg

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceSm
                                Text {
                                    text: "Custom mpv.conf options"
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "One option per line, e.g. \"deband=yes\". Applied on top of the generated configuration."
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.Wrap
                                }
                                TextArea {
                                    id: mpvArea
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 140
                                    text: root.mpvOptionsText
                                    onTextChanged: root.mpvOptionsText = text
                                    color: Theme.textPrimary
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: TextArea.Wrap
                                    selectByMouse: true
                                    padding: Theme.spaceSm
                                    background: Rectangle {
                                        color: Theme.bgInput
                                        radius: Theme.radiusMd
                                        border.width: 1
                                        border.color: mpvArea.activeFocus ? Theme.accent : Theme.border
                                    }
                                }
                                RowLayout {
                                    spacing: Theme.spaceSm
                                    AppButton {
                                        compact: true
                                        variant: "primary"
                                        iconName: "check"
                                        text: "Apply"
                                        onClicked: {
                                            app.customMpvOptions = mpvArea.text
                                            const errors = player.applyCustomMpvOptions(mpvArea.text)
                                            if (errors && errors.length > 0) {
                                                applyStatus.text = "Rejected: " + errors.join(", ")
                                                applyStatus.color = Theme.danger
                                            } else {
                                                applyStatus.text = "Applied — no errors."
                                                applyStatus.color = Theme.success
                                            }
                                        }
                                    }
                                    Text {
                                        id: applyStatus
                                        Layout.fillWidth: true
                                        text: ""
                                        color: Theme.textMuted
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fontCaption
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                            SettingRow {
                                title: "mpv log level"
                                description: "Verbosity of the embedded player log."
                                SettingCombo {
                                    model: root.logLevelModel
                                    currentIndex: Math.max(0, indexOfValue(app.mpvLogLevel))
                                    onActivated: app.mpvLogLevel = currentValue
                                }
                            }
                            SettingRow {
                                title: "Diagnostics"
                                description: "Copy playback and configuration details to the clipboard."
                                AppButton {
                                    variant: "secondary"
                                    iconName: "copy"
                                    text: "Copy Diagnostics"
                                    onClicked: player.copyDiagnosticsToClipboard()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
