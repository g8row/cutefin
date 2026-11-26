import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Cutefin

ApplicationWindow {
    id: mainWindow

    visible: true
    minimumWidth: 860
    minimumHeight: 560
    width: Math.max(860, app.savedWindowWidth > 0 ? app.savedWindowWidth : 1280)
    height: Math.max(560, app.savedWindowHeight > 0 ? app.savedWindowHeight : 780)
    visibility: app.savedWindowMaximized ? Window.Maximized : Window.Windowed
    title: (player.open && player.title.length > 0) ? (player.title + " — cutefin") : "cutefin"
    color: Theme.bgCanvas
    font.family: Theme.fontFamily

    property int normalWidth: width
    property int normalHeight: height
    property bool normalMaximized: app.savedWindowMaximized
    property bool enteredFullscreenForPlayer: false
    property int preFullscreenVisibility: Window.Windowed
    property bool libraryVisited: false

    onWidthChanged: {
        if (visibility === Window.Windowed && width >= minimumWidth)
            normalWidth = width
    }
    onHeightChanged: {
        if (visibility === Window.Windowed && height >= minimumHeight)
            normalHeight = height
    }
    onVisibilityChanged: function(visibility) {
        if (visibility === Window.Windowed) {
            normalMaximized = false
        } else if (visibility === Window.Maximized) {
            normalMaximized = true
        }
    }

    onClosing: function(closeEvent) {
        var saveMax = (visibility === Window.FullScreen)
            ? normalMaximized
            : (visibility === Window.Maximized)
        app.saveWindowState(normalWidth, normalHeight, saveMax)
        if (player.open)
            player.stopAndClose()
    }

    function enterPlayerFullscreen() {
        if (visibility === Window.FullScreen)
            return
        preFullscreenVisibility = (visibility === Window.Maximized)
            ? Window.Maximized
            : Window.AutomaticVisibility
        enteredFullscreenForPlayer = true
        visibility = Window.FullScreen
    }

    function exitPlayerFullscreen() {
        if (visibility !== Window.FullScreen) {
            enteredFullscreenForPlayer = false
            return
        }
        var restoreVis = (preFullscreenVisibility === Window.Maximized)
            ? Window.Maximized
            : Window.AutomaticVisibility
        enteredFullscreenForPlayer = false
        visibility = restoreVis
    }

    function togglePlayerFullscreen() {
        if (visibility === Window.FullScreen)
            exitPlayerFullscreen()
        else
            enterPlayerFullscreen()
    }

    function isTextInputFocused() {
        var item = mainWindow.activeFocusItem
        if (!item)
            return false
        if (item.hasOwnProperty("cursorPosition") && item.hasOwnProperty("readOnly"))
            return !item.readOnly
        return false
    }

    function focusSearchInput() {
        var page = searchLoader.item
        if (!page)
            return
        if (typeof page.focusSearch === "function") {
            page.focusSearch()
            return
        }
        if (typeof page.focusInput === "function") {
            page.focusInput()
            return
        }
        // Fallback: locate the first editable text input inside SearchPage.
        var queue = [page]
        while (queue.length > 0) {
            var node = queue.shift()
            if (node && node.hasOwnProperty("cursorPosition") && node.hasOwnProperty("readOnly") && !node.readOnly) {
                node.forceActiveFocus()
                return
            }
            if (node && node.children) {
                for (var i = 0; i < node.children.length; ++i)
                    queue.push(node.children[i])
            }
        }
        page.forceActiveFocus()
    }

    function openDebugDrawer(tab) {
        if (playerLoader.item && typeof playerLoader.item.toggleDrawer === "function")
            playerLoader.item.toggleDrawer(tab || "tracks")
    }

    function forwardToast(message, kind) {
        if (!toastHost)
            return
        if (typeof toastHost.show === "function")
            toastHost.show(message, kind)
        else if (typeof toastHost.push === "function")
            toastHost.push(message, kind)
        else if (typeof toastHost.showToast === "function")
            toastHost.showToast(message, kind)
        else if (typeof toastHost.addToast === "function")
            toastHost.addToast(message, kind)
    }

    Connections {
        target: app
        function onToast(message, kind) {
            mainWindow.forwardToast(message, kind)
        }
        function onAuthChanged() {
            if (!app.authenticated)
                mainWindow.libraryVisited = false
        }
        function onNavigationChanged() {
            if (app.authenticated && app.currentView === "library")
                mainWindow.libraryVisited = true
        }
    }

    Connections {
        target: player
        function onToast(message, kind) {
            mainWindow.forwardToast(message, kind)
        }
        function onOpenChanged() {
            if (player.open) {
                Qt.callLater(function() {
                    if (playerLoader.item)
                        playerLoader.item.forceActiveFocus()
                })
            } else {
                if (mainWindow.enteredFullscreenForPlayer || mainWindow.visibility === Window.FullScreen)
                    mainWindow.exitPlayerFullscreen()
            }
        }
    }

    // App-wide browse shortcuts (disabled while the integrated player is open)
    Shortcut {
        sequence: "/"
        enabled: app.authenticated && !player.open && !mainWindow.isTextInputFocused()
        onActivated: {
            app.openSearch()
            Qt.callLater(mainWindow.focusSearchInput)
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: app.authenticated && !player.open && app.canGoBack && !mainWindow.isTextInputFocused()
        onActivated: app.goBack()
    }

    Shortcut {
        sequence: "Alt+Left"
        enabled: app.authenticated && !player.open && app.canGoBack
        onActivated: app.goBack()
    }

    // -----------------------------------------------------------------------
    // Authentication flow (unauthenticated, player closed)
    // -----------------------------------------------------------------------
    Loader {
        id: authLoader
        anchors.fill: parent
        active: !app.authenticated && !player.open
        visible: active
        sourceComponent: app.currentView === "login" ? loginPageComp : serverSelectPageComp
    }

    Component {
        id: serverSelectPageComp
        ServerSelectPage {}
    }

    Component {
        id: loginPageComp
        LoginPage {}
    }

    // -----------------------------------------------------------------------
    // Authenticated Browse Shell (hidden while player is open to save CPU/GPU)
    // -----------------------------------------------------------------------
    RowLayout {
        id: browseShell
        anchors.fill: parent
        spacing: 0
        visible: app.authenticated && !player.open

        SidebarNav {
            id: sidebarNav
            Layout.fillHeight: true
            Layout.preferredWidth: compact ? 72 : 240
            compact: mainWindow.width < 1100
            activeKey: app.activeNavKey
            librariesModel: app.librariesModel
            userName: app.userName
            serverName: app.serverName

            onNavSelected: function(key) {
                if (key === "home")
                    app.openHome()
                else if (key === "search") {
                    app.openSearch()
                    Qt.callLater(mainWindow.focusSearchInput)
                } else if (key === "favorites")
                    app.openFavorites()
                else if (key === "settings")
                    app.openSettings()
            }

            onLibrarySelected: function(id, name, collectionType) {
                app.openLibrary(id, name, collectionType)
            }

            onLogoutRequested: app.logout()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            TopBar {
                id: topBar
                Layout.fillWidth: true
                title: app.pageTitle
                canGoBack: app.canGoBack
                onBackClicked: app.goBack()
                onSearchClicked: {
                    app.openSearch()
                    Qt.callLater(mainWindow.focusSearchInput)
                }
            }

            Item {
                id: viewStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                readonly property string view: app.currentView

                Loader {
                    id: homeLoader
                    anchors.fill: parent
                    active: app.authenticated
                    visible: viewStack.view === "home" || (
                        viewStack.view !== "library" &&
                        viewStack.view !== "detail" &&
                        viewStack.view !== "search" &&
                        viewStack.view !== "settings"
                    )
                    sourceComponent: homePageComp
                }

                Loader {
                    id: libraryLoader
                    anchors.fill: parent
                    active: app.authenticated && (mainWindow.libraryVisited || viewStack.view === "library")
                    visible: viewStack.view === "library"
                    sourceComponent: libraryPageComp
                }

                Loader {
                    id: detailLoader
                    anchors.fill: parent
                    active: app.authenticated && viewStack.view === "detail"
                    visible: active
                    sourceComponent: detailPageComp
                }

                Loader {
                    id: searchLoader
                    anchors.fill: parent
                    active: app.authenticated && viewStack.view === "search"
                    visible: active
                    sourceComponent: searchPageComp
                }

                Loader {
                    id: settingsLoader
                    anchors.fill: parent
                    active: app.authenticated && viewStack.view === "settings"
                    visible: active
                    sourceComponent: settingsPageComp
                }
            }
        }
    }

    Component {
        id: homePageComp
        HomePage {}
    }

    Component {
        id: libraryPageComp
        LibraryPage {}
    }

    Component {
        id: detailPageComp
        DetailPage {}
    }

    Component {
        id: searchPageComp
        SearchPage {}
    }

    Component {
        id: settingsPageComp
        SettingsPage {}
    }

    // -----------------------------------------------------------------------
    // Integrated Full-Window libmpv Player (lazy-loaded to save RAM while browsing)
    // -----------------------------------------------------------------------
    Loader {
        id: playerLoader
        anchors.fill: parent
        active: player.open
        visible: active
        z: 50
        sourceComponent: playerViewComp
        onLoaded: {
            if (item)
                item.forceActiveFocus()
        }
    }

    Component {
        id: playerViewComp
        PlayerView {
            targetWindow: mainWindow
        }
    }

    // -----------------------------------------------------------------------
    // Global Toast Overlay
    // -----------------------------------------------------------------------
    ToastHost {
        id: toastHost
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceLg
        z: 100
    }
}
