import QtQuick
import CustomMediaPlayer

// A hairline. Its own component so the one colour decision is made once, and so
// a vertical rule does not have to be written as a Rectangle with a magic width.
Rectangle {
    property bool vertical: false
    implicitWidth: vertical ? Theme.stroke.hairline : 0
    implicitHeight: vertical ? 0 : Theme.stroke.hairline
    color: Theme.color.borderSubtle
}
