import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Templates as T
import Subixa

// A themed menu that stays on screen.
//
// Derived from the Basic style's Menu rather than from QtQuick.Templates, and
// that is a correction rather than a preference. Built on the bare template it
// needs its own `contentItem`, and the obvious one -- a ListView over
// `contentModel` -- silently collapsed: a vertical ListView's `contentWidth` is
// not the width of its delegates, so sizing the popup from it produced a menu
// that reported `visible: true`, `opened: true` and a correct item count while
// occupying no space at all. Nothing errored, nothing drew, and every button in
// the app that opens a menu did nothing when pressed.
//
// Basic's Menu already solves the sizing, scrolling and keyboard navigation. All
// this needs to change is how it looks, which is `background` and the delegate.
C.Menu {
    id: menu

    // Where the menu prefers to open relative to its anchor item.
    property bool preferAbove: true
    property int maximumHeight: 420

    // Rendered inside the window, explicitly.
    //
    // Qt 6.8 added `popupType`, and 6.9 defaults a Menu to `Native`: it asks the
    // platform for a real menu and, where the platform has one, ignores
    // `background` and every delegate here. Under WSLg there is no native menu
    // implementation, so `popup()` returned successfully and drew *nothing* --
    // no warning, no error. `Item` keeps the menu in the window's overlay, so it
    // is styled by the tokens here on every platform and is visible to a
    // PrintWindow capture, which is the only way this project can look at its
    // own UI.
    popupType: T.Popup.Item

    padding: Theme.space.xs
    overlap: 0

    // Opens the menu against `item`: above it when there is room, below it when
    // there is not, and never off the edge of the window.
    //
    // The parent is set explicitly and the position computed in that same
    // coordinate space, because `Popup.x`/`y` are relative to the popup's
    // *parent item* while `mapToItem(null, ...)` returns window coordinates.
    // Mixing the two put the menu at x=1583, y=-210 -- fully built, fully
    // opaque, `opened: true`, and entirely outside the window. It looked exactly
    // like a menu that would not open, which is how it survived review: nothing
    // errors, and every property you would think to check reads correct.
    function popupNear(item) {
        const win = item.Window.window
        if (!win) {
            menu.popup(item)
            return
        }
        const surface = win.contentItem
        const pos = item.mapToItem(surface, 0, 0)
        const wanted = Math.min(menu.implicitHeight, menu.maximumHeight)

        menu.parent = surface
        // Clamped so a menu opened from a button near the right edge -- the
        // transport's overflow is the rightmost control there is -- stays on
        // screen instead of hanging off it.
        menu.x = Math.max(Theme.space.md,
                          Math.min(pos.x, surface.width - menu.width - Theme.space.md))
        menu.y = (menu.preferAbove && pos.y > wanted + Theme.space.md)
                 ? pos.y - wanted - Theme.space.sm
                 : Math.min(pos.y + item.height + Theme.space.sm,
                            surface.height - wanted - Theme.space.md)
        menu.open()
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0; to: 1
            duration: Theme.motion.base; easing.type: Theme.motion.standard
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"; from: 1; to: 0
            duration: Theme.motion.fast; easing.type: Theme.motion.standard
        }
    }

    background: Rectangle {
        implicitWidth: 220
        color: Theme.color.bgOverlay
        radius: Theme.radius.lg
        border.width: Theme.stroke.hairline
        border.color: Theme.color.border
    }
}
