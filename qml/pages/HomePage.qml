import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root
    color: Theme.bgCanvas

    ListView {
        id: list
        anchors.fill: parent
        model: app.homeSectionsModel
        spacing: Theme.spaceLg
        topMargin: 0
        bottomMargin: Theme.spaceXl
        cacheBuffer: 600
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        property bool userScrolled: false
        onDraggingChanged: if (dragging) userScrolled = true
        onFlickingChanged: if (flicking) userScrolled = true

        header: Item {
            id: heroHeader
            width: list.width
            height: hero.visible ? hero.height + Theme.spaceLg : Theme.spaceSm
            onHeightChanged: {
                if (!list.userScrolled)
                    Qt.callLater(function() { list.positionViewAtBeginning() })
            }

            HeroBanner {
                id: hero
                visible: !!(app.heroItem && app.heroItem.id)
                width: parent.width
                item: app.heroItem
                onPlayClicked: function(itemId, resume) { app.playItem(itemId, resume) }
                onDetailsClicked: function(itemId) { app.openDetail(itemId) }
            }
        }

        delegate: Item {
            id: sectionDelegate
            required property string title
            required property var itemsModel
            required property bool landscape
            required property string libraryId

            width: list.width
            height: mediaRow.height

            MediaRow {
                id: mediaRow
                width: parent.width
                title: sectionDelegate.title
                model: sectionDelegate.itemsModel
                landscape: sectionDelegate.landscape
                libraryId: sectionDelegate.libraryId
                onItemActivated: function(itemId) { app.openDetail(itemId) }
                onItemPlayClicked: function(itemId, resume) { app.playItem(itemId, resume) }
                onSeeAllClicked: function(libraryId, title) { app.openLibrary(libraryId, title, "") }
            }
        }
    }

    // Indeterminate top progress bar while (re)loading.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 2
        color: Theme.bgElevated
        visible: app.homeLoading
        Rectangle {
            id: homeProgress
            width: 140
            height: parent.height
            color: Theme.accent
            x: -width
            SequentialAnimation on x {
                running: app.homeLoading && Theme.animNormal > 0
                loops: Animation.Infinite
                NumberAnimation { from: -140; to: list.width; duration: 1400; easing.type: Easing.InOutQuad }
            }
        }
    }

    // Loading spinner
    AppIcon {
        anchors.centerIn: parent
        name: "refresh"
        size: 44
        color: Theme.accent
        visible: app.homeLoading && app.homeSectionsModel.count === 0
        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: app.homeLoading && Theme.animNormal > 0
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(480, root.width - Theme.spaceXl * 2)
        visible: !app.homeLoading && app.homeSectionsModel.count === 0
        iconName: "film"
        title: "Nothing to watch yet"
        subtitle: "Your Continue Watching and library rows will appear here once they finish loading."
        actionText: "Refresh"
        onActionClicked: app.refreshHome()
    }
}
