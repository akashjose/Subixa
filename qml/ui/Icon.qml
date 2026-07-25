import QtQuick
import QtQuick.Shapes
import CustomMediaPlayer

// One icon, tinted, at one of three sizes.
//
// The paths are authored on a 24x24 grid (see Icons.qml) and scaled here, so a
// 16px and a 24px icon are the same geometry rather than two drawings, and the
// stroke thins proportionally instead of going chunky at small sizes.
Item {
    id: root

    property string name: ""
    property int size: 20
    property color color: Theme.color.textSecondary
    // Animated so an icon that changes colour with its button's state does it
    // in step with the button's own fill rather than snapping ahead of it.
    property bool animateColor: true

    implicitWidth: size
    implicitHeight: size

    readonly property var glyph: Icons.glyphs[root.name]

    Behavior on color {
        enabled: root.animateColor
        ColorAnimation {
            duration: Theme.motion.fast
            easing.type: Theme.motion.standard
        }
    }

    Shape {
        anchors.fill: parent
        // The default geometry renderer rather than CurveRenderer: these are
        // static paths of a few dozen segments, and the geometry renderer is the
        // cheaper of the two on a software rasterizer.
        antialiasing: true

        transform: Scale {
            xScale: root.size / 24
            yScale: root.size / 24
        }

        // Filled component. strokeWidth -1 turns stroking off entirely rather
        // than drawing a hairline over the fill.
        ShapePath {
            fillColor: root.color
            strokeWidth: -1
            fillRule: ShapePath.WindingFill
            PathSvg { path: root.glyph && root.glyph.f ? root.glyph.f : "" }
        }

        // Stroked component.
        ShapePath {
            strokeColor: root.color
            fillColor: "transparent"
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.glyph && root.glyph.s ? root.glyph.s : "" }
        }
    }
}
