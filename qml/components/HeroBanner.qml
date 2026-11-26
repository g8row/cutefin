import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Cutefin

Item {
    id: root

    property var item

    signal playClicked(string itemId, bool resume)
    signal detailsClicked(string itemId)

    readonly property string itemId: (item && item.id) ? String(item.id) : ""
    readonly property string itemName: (item && item.name) ? String(item.name) : ""
    readonly property string overview: (item && item.overview) ? String(item.overview) : ""
    readonly property int year: (item && item.year) ? Number(item.year) : 0
    readonly property string officialRating: (item && item.officialRating) ? String(item.officialRating) : ""
    readonly property string runtimeText: (item && item.runtimeText) ? String(item.runtimeText) : ""
    readonly property real communityRating: (item && item.communityRating !== undefined) ? Number(item.communityRating) : -1
    readonly property string ratingText: (item && item.ratingText) ? String(item.ratingText) : ""
    readonly property string logoTag: (item && item.logoTag) ? String(item.logoTag) : ""
    readonly property string backdropTag: (item && item.backdropTag) ? String(item.backdropTag) : ""
    readonly property string backdropItemId: (item && item.backdropItemId) ? String(item.backdropItemId) : ""
    readonly property string backdropBlurHash: (item && item.backdropBlurHash) ? String(item.backdropBlurHash) : ""
    readonly property string thumbTag: (item && item.thumbTag) ? String(item.thumbTag) : ""
    readonly property string primaryTag: (item && item.primaryTag) ? String(item.primaryTag) : ""
    readonly property real playedPct: (item && item.playedPercentage !== undefined) ? Number(item.playedPercentage) : 0
    readonly property real playbackTicks: (item && item.playbackPositionTicks !== undefined) ? Number(item.playbackPositionTicks) : 0
    readonly property string resumePosText: (item && item.resumePositionText) ? String(item.resumePositionText) : ""
    readonly property bool canResume: playbackTicks > 0 || playedPct > 0

    readonly property real _dpr: Screen.devicePixelRatio || 1
    readonly property int backdropRequestWidth: Math.min(1600, Math.max(200, Math.ceil(((width > 0 ? width : 800) * _dpr) / 200) * 200))
    readonly property int primaryRequestWidth: Math.min(800, Math.max(200, Math.ceil((400 * _dpr) / 100) * 100))
    readonly property int logoRequestWidth: Math.min(800, Math.max(200, Math.ceil((340 * _dpr) / 100) * 100))

    readonly property string resolvedBackdropUrl: {
        const targetId = backdropItemId.length > 0 ? backdropItemId : itemId;
        if (backdropTag.length > 0 && targetId.length > 0)
            return Theme.imageUrl(targetId, "Backdrop", backdropTag, backdropRequestWidth);
        if (thumbTag.length > 0 && itemId.length > 0)
            return Theme.imageUrl(itemId, "Thumb", thumbTag, Math.min(1280, backdropRequestWidth));
        if (primaryTag.length > 0 && itemId.length > 0)
            return Theme.imageUrl(itemId, "Primary", primaryTag, primaryRequestWidth);
        return "";
    }

    readonly property string resolvedRatingText: {
        if (ratingText.length > 0)
            return ratingText;
        if (communityRating > 0)
            return communityRating.toFixed(1);
        return "";
    }

    implicitHeight: 400
    height: 400
    clip: true

    // Base cinema fill
    Rectangle {
        anchors.fill: parent
        color: Theme.bgSurface
    }

    // BlurHash backdrop placeholder
    Image {
        id: blurBackdrop
        anchors.fill: parent
        source: Theme.blurHashUrl(root.backdropBlurHash)
        asynchronous: false
        smooth: true
        fillMode: Image.PreserveAspectCrop
        visible: source !== "" && backdropImage.status !== Image.Ready
    }

    // Async Jellyfin Backdrop
    Image {
        id: backdropImage
        anchors.fill: parent
        source: root.resolvedBackdropUrl
        asynchronous: true
        cache: false
        smooth: true
        fillMode: Image.PreserveAspectCrop
        opacity: status === Image.Ready ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animFast }
        }
    }

    // Left-to-right gradient scrim
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.bgCanvas }
            GradientStop { position: 0.38; color: Qt.rgba(Theme.bgCanvas.r, Theme.bgCanvas.g, Theme.bgCanvas.b, 0.84) }
            GradientStop { position: 0.72; color: Qt.rgba(Theme.bgCanvas.r, Theme.bgCanvas.g, Theme.bgCanvas.b, 0.30) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Bottom-to-top gradient scrim fading seamlessly into Theme.bgCanvas
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.45; color: "transparent" }
            GradientStop { position: 0.82; color: Qt.rgba(Theme.bgCanvas.r, Theme.bgCanvas.g, Theme.bgCanvas.b, 0.74) }
            GradientStop { position: 1.0; color: Theme.bgCanvas }
        }
    }

    // Spotlight content
    ColumnLayout {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spaceLg
        anchors.rightMargin: Theme.spaceLg
        anchors.bottomMargin: Theme.spaceLg
        width: Math.min(parent.width - Theme.spaceLg * 2, 580)
        spacing: Theme.spaceSm + 2
        visible: root.itemId.length > 0

        // Jellyfin ClearLogo (dark mode)
        Image {
            id: logoImage
            Layout.maximumWidth: 340
            Layout.maximumHeight: 74
            Layout.alignment: Qt.AlignLeft
            source: (Theme.isDark && root.logoTag.length > 0 && root.itemId.length > 0)
                ? Theme.imageUrl(root.itemId, "Logo", root.logoTag, root.logoRequestWidth)
                : ""
            asynchronous: true
            cache: true
            smooth: true
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignLeft
            visible: source !== "" && status === Image.Ready
        }

        // Bold display title fallback
        Label {
            Layout.fillWidth: true
            visible: !logoImage.visible
            text: root.itemName
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontDisplay
            font.weight: Font.Bold
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        // Metadata pills row
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spaceSm
            visible: root.resolvedRatingText.length > 0 || root.year > 0 || root.officialRating.length > 0 || root.runtimeText.length > 0

            BadgePill {
                visible: root.resolvedRatingText.length > 0
                iconName: "star"
                text: root.resolvedRatingText
                textColor: Theme.warning
                tint: Theme.bgElevated
            }

            BadgePill {
                visible: root.year > 0
                text: String(root.year)
                textColor: Theme.textPrimary
                tint: Theme.bgElevated
            }

            BadgePill {
                visible: root.officialRating.length > 0
                text: root.officialRating
                textColor: Theme.textSecondary
                tint: Theme.bgElevated
            }

            BadgePill {
                visible: root.runtimeText.length > 0
                text: root.runtimeText
                textColor: Theme.textSecondary
                tint: Theme.bgElevated
            }
        }

        // 2-line overview
        Label {
            Layout.fillWidth: true
            visible: root.overview.length > 0
            text: root.overview
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            lineHeight: 1.35
        }

        // Primary CTA row
        RowLayout {
            Layout.topMargin: Theme.spaceXs
            spacing: Theme.spaceSm + 4

            AppButton {
                variant: "primary"
                iconName: "play"
                text: root.canResume
                    ? (root.resumePosText.length > 0 ? ("Resume · " + root.resumePosText) : "Resume")
                    : "Play"
                onClicked: root.playClicked(root.itemId, root.canResume)
            }

            AppButton {
                variant: "secondary"
                iconName: "info"
                text: "More Info"
                onClicked: root.detailsClicked(root.itemId)
            }
        }
    }
}
