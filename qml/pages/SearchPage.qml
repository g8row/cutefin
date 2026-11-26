import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cutefin

Rectangle {
    id: root
    color: Theme.bgCanvas

    readonly property bool hasQuery: app.searchQuery.trim().length > 0
    readonly property bool anyResults: app.searchMoviesModel.count > 0
        || app.searchSeriesModel.count > 0
        || app.searchEpisodesModel.count > 0
        || app.searchPeopleModel.count > 0

    function focusSearch() {
        searchField.focusInput()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceLg

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            AppTextField {
                id: searchField
                Layout.fillWidth: true
                implicitHeight: 48
                iconName: "search"
                placeholderText: "Search movies, shows, episodes, people…"
                text: app.searchQuery
                onTextChanged: {
                    if (text !== app.searchQuery)
                        app.setSearchQuery(text)
                }
                onAccepted: app.setSearchQuery(text)
                Keys.onEscapePressed: {
                    text = ""
                    app.setSearchQuery("")
                }
            }
        }

        // Recent searches (only while the query is empty)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm
            visible: !root.hasQuery && app.recentSearches.length > 0

            Text {
                text: "Recent searches"
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Repeater {
                    model: app.recentSearches
                    delegate: BadgePill {
                        text: modelData
                        iconName: "search"
                        clickable: true
                        tint: Theme.bgCard
                        textColor: Theme.textSecondary
                        onClicked: {
                            searchField.text = modelData
                            app.setSearchQuery(modelData)
                        }
                    }
                }
                AppButton {
                    compact: true
                    variant: "ghost"
                    text: "Clear"
                    onClicked: app.clearRecentSearches()
                }
            }
        }

        // Results
        Flickable {
            id: resultsFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: resultsCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick

            ColumnLayout {
                id: resultsCol
                x: 0
                y: 0
                width: resultsFlick.width
                spacing: Theme.spaceXl

                MediaRow {
                    Layout.fillWidth: true
                    visible: app.searchMoviesModel.count > 0
                    title: "Movies"
                    model: app.searchMoviesModel
                    onItemActivated: function(itemId) { app.openDetail(itemId) }
                    onItemPlayClicked: function(itemId, resume) { app.playItem(itemId, resume) }
                }
                MediaRow {
                    Layout.fillWidth: true
                    visible: app.searchSeriesModel.count > 0
                    title: "Series"
                    model: app.searchSeriesModel
                    onItemActivated: function(itemId) { app.openDetail(itemId) }
                    onItemPlayClicked: function(itemId, resume) { app.playItem(itemId, resume) }
                }
                MediaRow {
                    Layout.fillWidth: true
                    visible: app.searchEpisodesModel.count > 0
                    title: "Episodes"
                    model: app.searchEpisodesModel
                    landscape: true
                    onItemActivated: function(itemId) { app.openDetail(itemId) }
                    onItemPlayClicked: function(itemId, resume) { app.playItem(itemId, resume) }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm
                    visible: app.searchPeopleModel.count > 0

                    Text {
                        text: "People"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontH2
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        id: peopleList
                        Layout.fillWidth: true
                        Layout.preferredHeight: 152
                        orientation: ListView.Horizontal
                        model: app.searchPeopleModel
                        spacing: Theme.spaceMd
                        clip: true
                        cacheBuffer: 400
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: Item {
                            width: 104
                            height: 152

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: Theme.spaceSm

                                Item {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.preferredWidth: 84
                                    Layout.preferredHeight: 84
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 42
                                        color: Theme.bgCard
                                        border.width: 1
                                        border.color: Theme.border
                                        clip: true
                                        Image {
                                            id: personBlurImg
                                            anchors.fill: parent
                                            source: Theme.blurHashUrl(model.primaryBlurHash || "")
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: false
                                            visible: source !== "" && personImg.status !== Image.Ready
                                        }
                                        Image {
                                            id: personImg
                                            anchors.fill: parent
                                            source: Theme.imageUrl(model.id, "Primary", model.primaryTag || "", 168)
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            visible: model.primaryTag !== undefined && String(model.primaryTag).length > 0
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            visible: model.primaryTag === undefined || String(model.primaryTag).length === 0
                                            text: model.name ? String(model.name).charAt(0).toUpperCase() : "?"
                                            color: Theme.textSecondary
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontH1
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: model.name ? String(model.name) : ""
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: app.openPerson(String(model.id), String(model.name))
                            }
                        }
                    }
                }
            }
        }
    }

    // Loading spinner
    AppIcon {
        anchors.centerIn: parent
        name: "refresh"
        size: 40
        color: Theme.accent
        visible: app.searchLoading
        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: app.searchLoading && Theme.animNormal > 0
        }
    }

    // Empty states
    EmptyState {
        anchors.centerIn: parent
        width: Math.min(480, root.width - Theme.spaceXl * 2)
        visible: root.hasQuery && !app.searchLoading && !root.anyResults
        iconName: "search"
        title: "No results for \"" + app.searchQuery + "\""
        subtitle: "Try a different title, person, or genre."
    }
    EmptyState {
        anchors.centerIn: parent
        width: Math.min(480, root.width - Theme.spaceXl * 2)
        visible: !root.hasQuery && app.recentSearches.length === 0
        iconName: "search"
        title: "Search your library"
        subtitle: "Find movies, shows, episodes and people across every library."
    }
}
