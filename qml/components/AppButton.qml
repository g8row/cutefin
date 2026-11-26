import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Control {
    id: root

    property string text: ""
    property string iconName: ""
    property string variant: "secondary" // "primary", "secondary", "ghost", "danger"
    property bool compact: false
    property bool loading: false

    signal clicked()

    readonly property bool _interactive: enabled && !loading
    readonly property bool _hovered: hoverArea.containsMouse && _interactive
    readonly property bool _pressed: hoverArea.pressed && _interactive

    implicitWidth: Math.max(compact ? 32 : 84, contentRow.implicitWidth + leftPadding + rightPadding)
    implicitHeight: compact ? 34 : 40
    leftPadding: compact ? 12 : 18
    rightPadding: compact ? 12 : 18

    activeFocusOnTab: enabled
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1.0 : 0.48
    scale: _pressed ? 0.97 : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic }
    }

    Keys.onReturnPressed: function(event) {
        if (_interactive) {
            event.accepted = true;
            root.clicked();
        }
    }
    Keys.onEnterPressed: function(event) {
        if (_interactive) {
            event.accepted = true;
            root.clicked();
        }
    }
    Keys.onSpacePressed: function(event) {
        if (_interactive) {
            event.accepted = true;
            root.clicked();
        }
    }

    background: Item {
        // Keyboard focus ring (shader-free)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: Theme.radiusMd + 3
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: root.visualFocus || root.activeFocus
        }

        // Primary Jellyfin purple -> cyan gradient fill
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            visible: root.variant === "primary"
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop {
                    position: 0.0
                    color: root._hovered ? Theme.accentHover : Theme.accent
                }
                GradientStop {
                    position: 1.0
                    color: root._hovered ? Qt.lighter(Theme.accentSecondary, 1.10) : Theme.accentSecondary
                }
            }
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, root._hovered ? 0.28 : 0.16)

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "#000000"
                opacity: root._pressed ? 0.14 : 0.0
            }
        }

        // Secondary / Ghost / Danger surface fill
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            visible: root.variant !== "primary"
            color: {
                if (root.variant === "danger") {
                    return root._pressed
                        ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.30)
                        : (root._hovered
                           ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.24)
                           : Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.14));
                }
                if (root.variant === "ghost") {
                    return root._pressed
                        ? Theme.bgCardHover
                        : (root._hovered ? Theme.bgElevated : "transparent");
                }
                return root._pressed ? Theme.bgCard : (root._hovered ? Theme.bgCardHover : Theme.bgElevated);
            }
            border.width: root.variant === "ghost" && !root._hovered ? 0 : 1
            border.color: {
                if (root.variant === "danger")
                    return Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, root._hovered ? 0.70 : 0.42);
                return root._hovered ? Theme.borderStrong : Theme.border;
            }

            Behavior on color {
                ColorAnimation { duration: Theme.animFast }
            }
        }
    }

    contentItem: RowLayout {
        id: contentRow
        spacing: (root.iconName || root.loading) && root.text.length > 0 ? Theme.spaceSm : 0

        Item {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: root.compact ? 15 : 16
            implicitHeight: root.compact ? 15 : 16
            visible: root.loading || root.iconName.length > 0

            AppIcon {
                anchors.centerIn: parent
                size: root.compact ? 15 : 16
                name: root.loading ? "refresh" : root.iconName
                color: {
                    if (root.variant === "primary")
                        return "#FFFFFF";
                    if (root.variant === "danger")
                        return Theme.danger;
                    if (root.variant === "ghost" && !root._hovered)
                        return Theme.textSecondary;
                    return Theme.textPrimary;
                }

                RotationAnimator on rotation {
                    from: 0
                    to: 360
                    duration: 900
                    loops: Animation.Infinite
                    running: root.loading && Theme.animNormal > 0
                }
            }
        }

        Label {
            Layout.alignment: Qt.AlignVCenter
            visible: root.text.length > 0
            text: root.text
            font.family: Theme.fontFamily
            font.pixelSize: root.compact ? Theme.fontSmall : Theme.fontBody
            font.weight: Font.DemiBold
            color: {
                if (root.variant === "primary")
                    return "#FFFFFF";
                if (root.variant === "danger")
                    return Theme.danger;
                if (root.variant === "ghost" && !root._hovered)
                    return Theme.textSecondary;
                return Theme.textPrimary;
            }
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        enabled: root._interactive
        cursorShape: root._interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
