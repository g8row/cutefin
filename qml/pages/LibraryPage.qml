import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root
    color: Theme.bgCanvas

    // Shared styled dropdown for the filter toolbar.
    component FilterCombo : ComboBox {
        id: combo
        implicitWidth: 150
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

    readonly property bool isLandscape: app.libraryViewMode === "landscape"
    readonly property bool filtersActive: app.libraryGenre.length > 0
        || app.libraryYear > 0
        || app.libraryRating.length > 0
        || app.libraryPlayedFilter !== "any"
        || app.libraryFavoritesOnly

    readonly property var sortModel: [
        { label: "Name", value: "SortName" },
        { label: "Date added", value: "DateCreated" },
        { label: "Release date", value: "PremiereDate" },
        { label: "Rating", value: "CommunityRating" },
        { label: "Runtime", value: "Runtime" },
        { label: "Random", value: "Random" }
    ]
    readonly property var genreModel: [{ label: "All Genres", value: "" }]
        .concat(app.availableGenres.map(function(g) { return { label: g, value: g } }))
    readonly property var yearModel: [{ label: "All Years", value: 0 }]
        .concat(app.availableYears.map(function(y) { return { label: String(y), value: y } }))
    readonly property var ratingModel: [{ label: "All Ratings", value: "" }]
        .concat(app.availableRatings.map(function(r) { return { label: r, value: r } }))
    readonly property var playedModel: [
        { label: "Any", value: "any" },
        { label: "Unplayed", value: "unplayed" },
        { label: "Played", value: "played" }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Header toolbar ---
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: headerContent.implicitHeight + Theme.spaceMd * 2
            color: Theme.bgSurface
            border.width: 0
            z: 2

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }

            ColumnLayout {
                id: headerContent
                anchors.fill: parent
                anchors.margins: Theme.spaceMd
                anchors.leftMargin: Theme.spaceLg
                anchors.rightMargin: Theme.spaceLg
                spacing: Theme.spaceSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd

                    Text {
                        text: app.pageTitle
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontH1
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: app.libraryTotalCount + (app.libraryTotalCount === 1 ? " item" : " items")
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    Item { Layout.fillWidth: true }

                    // Poster / Landscape view toggle
                    AppButton {
                        compact: true
                        iconName: "grid"
                        variant: !root.isLandscape ? "secondary" : "ghost"
                        onClicked: app.libraryViewMode = "poster"
                    }
                    AppButton {
                        compact: true
                        iconName: "list"
                        variant: root.isLandscape ? "secondary" : "ghost"
                        onClicked: app.libraryViewMode = "landscape"
                    }

                    FilterCombo {
                        model: root.sortModel
                        currentIndex: Math.max(0, indexOfValue(app.librarySortBy))
                        onActivated: app.setLibrarySort(currentValue, app.libraryAscending)
                    }
                    AppButton {
                        compact: true
                        variant: "ghost"
                        text: app.libraryAscending ? "Asc" : "Desc"
                        onClicked: app.setLibrarySort(app.librarySortBy, !app.libraryAscending)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    FilterCombo {
                        model: root.genreModel
                        currentIndex: Math.max(0, indexOfValue(app.libraryGenre))
                        onActivated: app.setLibraryGenre(currentValue)
                    }
                    FilterCombo {
                        model: root.yearModel
                        currentIndex: Math.max(0, indexOfValue(app.libraryYear))
                        onActivated: app.setLibraryYear(currentValue)
                    }
                    FilterCombo {
                        model: root.ratingModel
                        currentIndex: Math.max(0, indexOfValue(app.libraryRating))
                        onActivated: app.setLibraryRating(currentValue)
                    }
                    FilterCombo {
                        model: root.playedModel
                        currentIndex: Math.max(0, indexOfValue(app.libraryPlayedFilter))
                        onActivated: app.setLibraryPlayedFilter(currentValue)
                    }
                    BadgePill {
                        text: "Favorites"
                        iconName: app.libraryFavoritesOnly ? "heart-filled" : "heart"
                        clickable: true
                        tint: app.libraryFavoritesOnly ? Theme.accentMuted : Theme.bgCard
                        textColor: app.libraryFavoritesOnly ? Theme.accent : Theme.textSecondary
                        onClicked: app.setLibraryFavoritesOnly(!app.libraryFavoritesOnly)
                    }
                    AppButton {
                        compact: true
                        variant: "ghost"
                        iconName: "refresh"
                        text: "Reset"
                        enabled: root.filtersActive
                        onClicked: app.resetLibraryFilters()
                    }
                }
            }
        }

        // --- Virtualized grid ---
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: grid
                anchors.fill: parent
                anchors.margins: Theme.spaceMd
                model: app.libraryModel
                cacheBuffer: 400
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                property int columns: Math.max(1, Math.floor((width + Theme.spaceMd) / (root.isLandscape ? 320 : 184)))
                cellWidth: width / columns
                cellHeight: root.isLandscape
                    ? Math.round(cellWidth * 9 / 16) + 88
                    : Math.round(cellWidth * 1.5) + 88

                function checkLoadMore() {
                    if (contentY + height >= contentHeight - 500 && app.libraryHasMore && !app.libraryLoading)
                        app.loadMoreLibraryItems()
                }

                onContentYChanged: Qt.callLater(grid.checkLoadMore)
                onContentHeightChanged: Qt.callLater(grid.checkLoadMore)
                onCountChanged: Qt.callLater(grid.checkLoadMore)
                onHeightChanged: Qt.callLater(grid.checkLoadMore)

                delegate: Item {
                    id: cell
                    width: grid.cellWidth
                    height: grid.cellHeight

                    Loader {
                        id: cardLoader
                        anchors.horizontalCenter: parent.horizontalCenter
                        readonly property var cellModel: model

                        sourceComponent: root.isLandscape ? landscapeCardComp : posterCardComp

                        onLoaded: {
                            if (item)
                                item.item = Qt.binding(function() { return cardLoader.cellModel })
                        }
                    }

                    Connections {
                        target: cardLoader.item
                        function onActivated(itemId) {
                            const row = app.libraryModel.get(index)
                            if (row && row.id)
                                app.openItem(row)
                            else
                                app.openDetail(itemId)
                        }
                        function onPlayClicked(itemId, resume) {
                            app.playItem(itemId, resume)
                        }
                    }
                }
            }

            Component {
                id: posterCardComp

                PosterCard {
                    cardWidth: grid.cellWidth - Theme.spaceMd
                }
            }

            Component {
                id: landscapeCardComp

                LandscapeCard {
                    cardWidth: grid.cellWidth - Theme.spaceMd
                }
            }

            // Loading footer spinner
            AppIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spaceLg
                name: "refresh"
                size: 28
                color: Theme.accent
                visible: app.libraryLoading && app.libraryModel.count > 0
                RotationAnimator on rotation {
                    from: 0
                    to: 360
                    duration: 900
                    loops: Animation.Infinite
                    running: app.libraryLoading && Theme.animNormal > 0
                }
            }

            EmptyState {
                anchors.centerIn: parent
                width: Math.min(480, parent.width - Theme.spaceXl * 2)
                visible: !app.libraryLoading && app.libraryModel.count === 0
                iconName: "folder"
                title: "No items found"
                subtitle: root.filtersActive
                    ? "Try clearing the active filters to see everything in this library."
                    : "This library appears to be empty."
                actionText: root.filtersActive ? "Reset Filters" : ""
                onActionClicked: app.resetLibraryFilters()
            }
        }
    }
}
