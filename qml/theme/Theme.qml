pragma Singleton

import QtQuick
import QtQuick.Controls

QtObject {
    id: theme

    // Safe accessor for the C++ AppController context property (`app`) during early QML init.
    readonly property var _app: (typeof app !== "undefined" && app) ? app : null

    readonly property SystemPalette _sysPalette: SystemPalette {
        colorGroup: SystemPalette.Active
    }

    readonly property string _themeMode: _app ? _app.themeMode : "dark"
    readonly property bool _systemIsLight: (Application.styleHints && Application.styleHints.colorScheme !== undefined)
        ? (Application.styleHints.colorScheme === Qt.ColorScheme.Light
           || (Application.styleHints.colorScheme === Qt.ColorScheme.Unknown && _sysPalette.window.hslLightness > 0.5))
        : (_sysPalette.window.hslLightness > 0.5)

    readonly property bool isDark: !(_themeMode === "light" || (_themeMode === "system" && _systemIsLight))

    // --- Colors: Jellyfin Deep Cinema Dark + Clean Light ---
    readonly property color bgCanvas: isDark ? "#0B0D12" : "#F5F7FA"
    readonly property color bgSurface: isDark ? "#11151E" : "#FFFFFF"
    readonly property color bgElevated: isDark ? "#171C28" : "#EEF1F6"
    readonly property color bgCard: isDark ? "#1A202E" : "#FFFFFF"
    readonly property color bgCardHover: isDark ? "#232B3D" : "#F0F4FA"
    readonly property color bgOverlay: isDark ? "#E60B0D12" : "#D9111827"
    readonly property color bgInput: isDark ? "#141925" : "#FFFFFF"

    readonly property color border: isDark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.09)
    readonly property color borderStrong: isDark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.18)

    readonly property color accent: (_app && _app.accentColor) ? Qt.color(_app.accentColor) : Qt.color("#AA5CC3")
    readonly property color accentSecondary: isDark ? "#00A4DC" : "#008EC2"
    readonly property color accentHover: isDark ? Qt.lighter(accent, 1.14) : Qt.darker(accent, 1.10)
    readonly property color accentMuted: Qt.rgba(accent.r, accent.g, accent.b, isDark ? 0.18 : 0.14)

    readonly property color textPrimary: isDark ? "#F3F5F9" : "#111827"
    readonly property color textSecondary: isDark ? "#9AA4B8" : "#4B5563"
    readonly property color textMuted: isDark ? "#667085" : "#6B7280"

    readonly property color danger: isDark ? "#EF4444" : "#DC2626"
    readonly property color success: isDark ? "#22C55E" : "#16A34A"
    readonly property color warning: isDark ? "#F59E0B" : "#D97706"

    // --- Spacing & Radii ---
    readonly property int radiusSm: 6
    readonly property int radiusMd: 10
    readonly property int radiusLg: 14
    readonly property int radiusXl: 20

    readonly property int spaceXs: 4
    readonly property int spaceSm: 8
    readonly property int spaceMd: 16
    readonly property int spaceLg: 24
    readonly property int spaceXl: 32

    // --- Typography ---
    readonly property string fontFamily: Application.font.family
    readonly property string fontMono: Qt.platform.os === "osx" ? "Menlo" : "monospace"
    readonly property int fontDisplay: 30
    readonly property int fontH1: 24
    readonly property int fontH2: 18
    readonly property int fontBody: 14
    readonly property int fontSmall: 12
    readonly property int fontCaption: 11

    // --- Motion (Dual-Core & Reduce Motion aware) ---
    readonly property bool _reduceMotion: _app ? Boolean(_app.reduceMotion) : false
    readonly property bool _dualCoreMode: _app ? Boolean(_app.dualCoreMode) : false

    readonly property int animFast: (_reduceMotion || _dualCoreMode) ? 0 : 120
    readonly property int animNormal: _reduceMotion ? 0 : 180
    readonly property int animSlow: _reduceMotion ? 0 : 260

    // --- Helpers ---
    function imageUrl(itemId, type, tag, width) {
        if (!itemId || !tag)
            return "";
        const w = Math.max(200, Math.round(width || 400));
        return "image://jellyfin/" + encodeURIComponent(itemId) + "/" + type + "/" + encodeURIComponent(tag) + "/" + w;
    }

    function blurHashUrl(hash) {
        if (!hash)
            return "";
        return "image://blurhash/" + encodeURIComponent(hash);
    }

    function formatTime(seconds) {
        const total = Math.max(0, Math.floor(Number(seconds) || 0));
        const hrs = Math.floor(total / 3600);
        const mins = Math.floor((total % 3600) / 60);
        const secs = total % 60;
        const pad2 = function(n) { return n < 10 ? ("0" + n) : String(n); };
        if (hrs > 0)
            return hrs + ":" + pad2(mins) + ":" + pad2(secs);
        return mins + ":" + pad2(secs);
    }
}
