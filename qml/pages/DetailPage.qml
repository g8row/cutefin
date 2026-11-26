import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Cutefin

Rectangle {
    id: root
    color: Theme.bgCanvas

    readonly property var d: app.detailItem || ({})
    readonly property string itemId: d.id ? String(d.id) : ""
    readonly property string itemType: d.type ? String(d.type) : ""
    readonly property string itemName: d.name ? String(d.name) : ""
    readonly property bool resumable: Number(d.playbackPositionTicks || 0) > 0 || Number(d.playedPercentage || 0) > 0

    readonly property string resumeText: {
        const t = d.resumePositionText ? String(d.resumePositionText) : ""
        if (t.length > 0)
            return t
        const ticks = Number(d.playbackPositionTicks || 0)
        return ticks > 0 ? Theme.formatTime(ticks / 10000000) : ""
    }

    readonly property real _dpr: Screen.devicePixelRatio || 1
    readonly property int backdropRequestWidth: Math.min(1600, Math.max(200, Math.ceil(((width > 0 ? width : 800) * _dpr) / 200) * 200))
    readonly property int posterRequestWidth: Math.max(200, Math.ceil((200 * _dpr) / 100) * 100)
    readonly property int logoRequestWidth: Math.min(900, Math.max(200, Math.ceil((420 * _dpr) / 100) * 100))
    readonly property int stillRequestWidth: Math.max(200, Math.ceil((168 * _dpr) / 100) * 100)
    readonly property int castRequestWidth: Math.max(200, Math.ceil((84 * _dpr) / 100) * 100)

    readonly property string backdropUrl: {
        const tag = d.backdropTag ? String(d.backdropTag) : ""
        if (!tag)
            return ""
        const bid = d.backdropItemId ? String(d.backdropItemId) : itemId
        return Theme.imageUrl(bid, "Backdrop", tag, backdropRequestWidth)
    }
    readonly property string backdropHashUrl: Theme.blurHashUrl(d.backdropBlurHash ? String(d.backdropBlurHash) : "")
    readonly property string posterUrl: Theme.imageUrl(itemId, "Primary", d.primaryTag ? String(d.primaryTag) : "", posterRequestWidth)
    readonly property string posterHashUrl: Theme.blurHashUrl(d.primaryBlurHash ? String(d.primaryBlurHash) : "")
    readonly property string logoUrl: Theme.imageUrl(itemId, "Logo", d.logoTag ? String(d.logoTag) : "", logoRequestWidth)

    readonly property var techBadges: [d.resolutionBadge, d.videoCodecBadge, d.audioBadge, d.hdrBadge, d.containerBadge]
        .filter(function(b) { return b !== undefined && b !== null && String(b).length > 0 })
        .map(function(b) { return String(b) })
    readonly property var genreList: (d.genres && d.genres.length) ? d.genres : []
    readonly property var castArray: (d.cast && d.cast.length) ? d.cast : []
    readonly property bool isSeriesView: (itemType === "Series" || itemType === "Season") && app.seasonsModel.count > 0
    readonly property bool isCollectionView: itemType === "BoxSet" && app.episodesModel.count > 0

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: page.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Item {
            id: page
            width: flick.width
            height: backdrop.height + body.height - 110

            // --- Full-bleed backdrop hero ---
            Item {
                id: backdrop
                width: page.width
                height: 340

                Rectangle {
                    anchors.fill: parent
                    color: Theme.bgCard
                }
                Image {
                    anchors.fill: parent
                    source: root.backdropHashUrl
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: source !== ""
                }
                Image {
                    anchors.fill: parent
                    source: root.backdropUrl
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    opacity: status === Image.Ready ? 1.0 : 0.0
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.animNormal }
                    }
                }
                // Legibility darkening + fade into the canvas
                Rectangle {
                    anchors.fill: parent
                    color: "#000000"
                    opacity: 0.22
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.58; color: "transparent" }
                        GradientStop { position: 1.0; color: Theme.bgCanvas }
                    }
                }
            }

            // --- Body overlapping the backdrop ---
            Item {
                id: body
                y: backdrop.height - 110
                width: page.width
                height: bodyCol.implicitHeight + Theme.spaceXl

                ColumnLayout {
                    id: bodyCol
                    x: Theme.spaceXl
                    y: 0
                    width: parent.width - Theme.spaceXl * 2
                    spacing: Theme.spaceLg

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceXl

                        // Poster 200x300
                        Item {
                            Layout.preferredWidth: 200
                            Layout.preferredHeight: 300
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusMd
                                color: Theme.bgCard
                                border.width: 1
                                border.color: Theme.borderStrong
                                clip: true
                                AppIcon {
                                    anchors.centerIn: parent
                                    name: (root.itemType === "Series" || root.itemType === "Season") ? "tv"
                                        : (root.itemType === "Episode" ? "play" : "film")
                                    size: 36
                                    color: Theme.textMuted
                                    visible: root.posterUrl === ""
                                }
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    source: root.posterHashUrl
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    visible: source !== ""
                                }
                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    source: root.posterUrl
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: false
                                    opacity: status === Image.Ready ? 1.0 : 0.0
                                    Behavior on opacity {
                                        NumberAnimation { duration: Theme.animNormal }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceSm

                            // Logo or title
                            Image {
                                Layout.preferredHeight: 68
                                Layout.maximumWidth: 420
                                Layout.alignment: Qt.AlignLeft
                                source: root.logoUrl
                                fillMode: Image.PreserveAspectFit
                                horizontalAlignment: Image.AlignLeft
                                asynchronous: true
                                cache: false
                                visible: root.logoUrl !== ""
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: root.logoUrl === ""
                                text: root.itemName
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontDisplay
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: d.tagline !== undefined && String(d.tagline).length > 0
                                text: d.tagline ? String(d.tagline) : ""
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                font.italic: true
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            // Metadata row
                            RowLayout {
                                spacing: Theme.spaceSm
                                Text {
                                    visible: Number(d.year || 0) > 0
                                    text: String(d.year)
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                }
                                BadgePill {
                                    visible: d.officialRating !== undefined && String(d.officialRating).length > 0
                                    text: d.officialRating ? String(d.officialRating) : ""
                                    tint: Theme.bgElevated
                                    textColor: Theme.textSecondary
                                }
                                Text {
                                    visible: d.runtimeText !== undefined && String(d.runtimeText).length > 0
                                    text: d.runtimeText ? String(d.runtimeText) : ""
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                }
                                RowLayout {
                                    spacing: 4
                                    visible: Number(d.communityRating !== undefined ? d.communityRating : -1) >= 0
                                    AppIcon {
                                        name: "star"
                                        size: 14
                                        color: Theme.warning
                                    }
                                    Text {
                                        text: Number(d.communityRating).toFixed(1)
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontBody
                                        font.weight: Font.DemiBold
                                    }
                                }
                                BadgePill {
                                    visible: root.itemType.length > 0
                                    text: root.itemType === "CollectionFolder" ? "Collection" : root.itemType
                                    tint: Theme.bgElevated
                                    textColor: Theme.textMuted
                                }
                                // Series shortcut for episodes
                                BadgePill {
                                    visible: root.itemType === "Episode" && d.seriesId !== undefined && String(d.seriesId).length > 0
                                    text: d.seriesName ? String(d.seriesName) : "Series"
                                    iconName: "tv"
                                    clickable: true
                                    tint: Theme.accentMuted
                                    textColor: Theme.accent
                                    onClicked: app.openDetail(String(d.seriesId))
                                }
                            }

                            // Technical badges
                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.spaceSm
                                visible: root.techBadges.length > 0
                                Repeater {
                                    model: root.techBadges
                                    delegate: BadgePill {
                                        text: modelData
                                        tint: Theme.bgElevated
                                        textColor: Theme.textSecondary
                                    }
                                }
                            }

                            // Genres
                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.spaceSm
                                visible: root.genreList.length > 0
                                Repeater {
                                    model: root.genreList
                                    delegate: BadgePill {
                                        text: String(modelData)
                                        clickable: true
                                        tint: Theme.accentMuted
                                        textColor: Theme.accent
                                        onClicked: app.openGenre(String(modelData))
                                    }
                                }
                            }

                            // Action bar
                            RowLayout {
                                spacing: Theme.spaceSm
                                Layout.topMargin: Theme.spaceXs

                                AppButton {
                                    variant: "primary"
                                    iconName: "play"
                                    text: root.resumable && root.resumeText.length > 0
                                        ? "Resume from " + root.resumeText
                                        : "Play"
                                    onClicked: app.playItem(root.itemId, true)
                                }
                                AppButton {
                                    variant: "secondary"
                                    iconName: "play"
                                    text: "Play from Start"
                                    visible: root.resumable
                                    onClicked: app.playItem(root.itemId, false)
                                }
                                AppButton {
                                    variant: "secondary"
                                    iconName: "check"
                                    text: d.played ? "Mark Unwatched" : "Mark Watched"
                                    onClicked: app.togglePlayed(root.itemId, !!d.played)
                                }
                                AppButton {
                                    variant: "secondary"
                                    iconName: d.isFavorite ? "heart-filled" : "heart"
                                    text: d.isFavorite ? "Favorited" : "Favorite"
                                    onClicked: app.toggleFavorite(root.itemId, !!d.isFavorite)
                                }
                            }
                        }
                    }

                    // Overview
                    Text {
                        Layout.fillWidth: true
                        visible: d.overview !== undefined && String(d.overview).length > 0
                        text: d.overview ? String(d.overview) : ""
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                        lineHeight: 1.45
                    }

                    // --- Seasons + Episodes (Series) ---
                    Loader {
                        Layout.fillWidth: true
                        active: root.isSeriesView
                        visible: active
                        sourceComponent: seriesSectionComp
                    }

                    // --- Collection items (BoxSet) ---
                    MediaRow {
                        Layout.fillWidth: true
                        visible: root.isCollectionView
                        title: "Items in Collection"
                        model: root.isCollectionView ? app.episodesModel : null
                        onItemActivated: function(itemId) { app.openDetail(itemId) }
                        onItemPlayClicked: function(itemId, resume) { app.playItem(itemId, resume) }
                    }

                    // --- Cast & Crew ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceMd
                        visible: root.castArray.length > 0

                        Text {
                            text: "Cast & Crew"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontH2
                            font.weight: Font.DemiBold
                        }

                        ListView {
                            id: castList
                            Layout.fillWidth: true
                            Layout.preferredHeight: 174
                            orientation: ListView.Horizontal
                            model: root.castArray
                            spacing: Theme.spaceMd
                            clip: true
                            cacheBuffer: 400
                            boundsBehavior: Flickable.StopAtBounds

                            delegate: Item {
                                width: 100
                                height: 174

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 6

                                    Rectangle {
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.preferredWidth: 96
                                        Layout.preferredHeight: 122
                                        radius: Theme.radiusSm
                                        color: Theme.bgElevated
                                        border.width: 1
                                        border.color: castArea.containsMouse ? Theme.accent : Theme.border
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            source: Theme.imageUrl(modelData.id, "Primary", modelData.primaryTag || "", root.castRequestWidth)
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: false
                                            visible: modelData.primaryTag !== undefined && String(modelData.primaryTag).length > 0
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            visible: modelData.primaryTag === undefined || String(modelData.primaryTag).length === 0
                                            text: modelData.name ? String(modelData.name).charAt(0).toUpperCase() : "?"
                                            color: Theme.textSecondary
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontH1
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name ? String(modelData.name) : ""
                                        color: Theme.textPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: modelData.role !== undefined && String(modelData.role).length > 0
                                        text: modelData.role ? String(modelData.role) : ""
                                        color: Theme.textMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontCaption
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    id: castArea
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    hoverEnabled: true
                                    onClicked: app.openPerson(String(modelData.id), String(modelData.name))
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: seriesSectionComp

        ColumnLayout {
            spacing: Theme.spaceMd

            Text {
                text: "Seasons"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontH2
                font.weight: Font.DemiBold
            }

            ListView {
                id: seasonTabs
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                orientation: ListView.Horizontal
                model: app.seasonsModel
                spacing: Theme.spaceSm
                clip: true
                cacheBuffer: 300
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: seasonChip
                    readonly property string seasonId: String(model.id)
                    readonly property bool selected: app.selectedSeasonId === seasonId

                    width: seasonChipText.implicitWidth + Theme.spaceLg * 2
                    height: 36
                    radius: 18
                    color: selected ? Theme.accent : Theme.bgCard
                    border.width: 1
                    border.color: selected ? Theme.accent : Theme.border

                    Text {
                        id: seasonChipText
                        anchors.centerIn: parent
                        text: String(model.name || "")
                        color: seasonChip.selected ? "#FFFFFF" : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.weight: seasonChip.selected ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: app.selectSeason(seasonChip.seasonId)
                    }
                }
            }

            Text {
                text: "Episodes"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontH2
                font.weight: Font.DemiBold
            }

            ColumnLayout {
                id: episodesCol
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Repeater {
                    model: root.isSeriesView ? app.episodesModel : null

                    delegate: Rectangle {
                        id: epCard
                        Layout.fillWidth: true
                        implicitHeight: epRow.implicitHeight + Theme.spaceMd * 2
                        radius: Theme.radiusMd
                        color: Theme.bgCard
                        border.width: 1
                        border.color: Theme.border

                        readonly property string epCode: {
                            if (model.episodeCode)
                                return String(model.episodeCode)
                            const e = Number(model.indexNumber)
                            const s = Number(model.parentIndexNumber)
                            if (e < 0)
                                return ""
                            const pad = function(n) { return (n < 10 ? "0" : "") + n }
                            return (s >= 0 ? "S" + pad(s) : "") + "E" + pad(e)
                        }
                        readonly property string stillUrl: {
                            if (model.primaryTag)
                                return Theme.imageUrl(model.id, "Primary", model.primaryTag, root.stillRequestWidth)
                            if (model.thumbTag)
                                return Theme.imageUrl(model.id, "Thumb", model.thumbTag, root.stillRequestWidth)
                            if (model.backdropTag)
                                return Theme.imageUrl(model.backdropItemId || model.id, "Backdrop", model.backdropTag, root.stillRequestWidth)
                            return ""
                        }
                        readonly property string stillHash: {
                            if (model.primaryTag && model.primaryBlurHash)
                                return Theme.blurHashUrl(model.primaryBlurHash)
                            if (model.thumbTag && model.thumbBlurHash)
                                return Theme.blurHashUrl(model.thumbBlurHash)
                            if (model.backdropTag && model.backdropBlurHash)
                                return Theme.blurHashUrl(model.backdropBlurHash)
                            return ""
                        }
                        readonly property real progressPct: Number(model.playedPercentage || 0)

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: app.playItem(String(model.id), true)
                        }

                        RowLayout {
                            id: epRow
                            anchors.fill: parent
                            anchors.margins: Theme.spaceMd
                            spacing: Theme.spaceMd

                            // 16:9 still with progress bar
                            Item {
                                Layout.preferredWidth: 168
                                Layout.preferredHeight: 94
                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.radiusSm
                                    color: Theme.bgElevated
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: epCard.stillHash
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        visible: source !== ""
                                    }
                                    Image {
                                        anchors.fill: parent
                                        source: epCard.stillUrl
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: false
                                        opacity: status === Image.Ready ? 1.0 : 0.0
                                        Behavior on opacity {
                                            NumberAnimation { duration: Theme.animNormal }
                                        }
                                    }
                                    AppIcon {
                                        anchors.centerIn: parent
                                        name: "play"
                                        size: 22
                                        color: "#FFFFFF"
                                        visible: epCard.stillUrl === ""
                                    }
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 4
                                        color: Qt.rgba(1, 1, 1, 0.25)
                                        visible: epCard.progressPct > 0
                                        Rectangle {
                                            width: parent.width * Math.min(100, epCard.progressPct) / 100
                                            height: parent.height
                                            color: Theme.accent
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: (epCard.epCode.length > 0 ? epCard.epCode + "  ·  " : "") + String(model.name || "")
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    visible: text.length > 0
                                    text: [
                                        model.runtimeText ? String(model.runtimeText) : "",
                                        model.remainingText ? String(model.remainingText) : "",
                                        model.played ? "Watched" : ""
                                    ].filter(function(s) { return s.length > 0 }).join("  ·  ")
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: model.overview !== undefined && String(model.overview).length > 0
                                    text: model.overview ? String(model.overview) : ""
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }

                            AppButton {
                                Layout.alignment: Qt.AlignVCenter
                                compact: true
                                variant: "ghost"
                                iconName: "play"
                                onClicked: app.playItem(String(model.id), true)
                            }
                        }
                    }
                }
            }
        }
    }

    // Loading / empty overlays
    AppIcon {
        anchors.centerIn: parent
        name: "refresh"
        size: 44
        color: Theme.accent
        visible: app.detailLoading && root.itemId === ""
        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: app.detailLoading && Theme.animNormal > 0
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(480, root.width - Theme.spaceXl * 2)
        visible: !app.detailLoading && root.itemId === ""
        iconName: "info"
        title: "Nothing to show"
        subtitle: "This item could not be loaded from the server."
        actionText: "Go Back"
        onActionClicked: app.goBack()
    }
}
