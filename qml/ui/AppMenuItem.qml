import QtQuick
import QtQuick.Templates as T
import CustomMediaPlayer

// A menu row with a real shortcut column.
//
// The items this replaces carried their binding inside the label -- "Detach into
// its own window (Ctrl+D)" -- because Basic's MenuItem has nowhere else to put
// it. That reads as placeholder copy, produces ragged text, and cannot follow a
// remapped key.
T.MenuItem {
    id: item

    property string iconName: ""
    property string shortcutText: ""
    property bool danger: false

    implicitWidth: Math.max(220, row.implicitWidth + shortcutLabel.implicitWidth
                                 + 3 * Theme.space.lg)
    implicitHeight: 30
    padding: 0
    hoverEnabled: true

    readonly property color _tint: !item.enabled ? Theme.color.textDisabled
                                : item.danger ? Theme.color.error
                                : item.highlighted ? Theme.color.textPrimary
                                                   : Theme.color.textSecondary

    background: Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.space.xs
        anchors.rightMargin: Theme.space.xs
        radius: Theme.radius.md
        color: item.highlighted ? Theme.color.bgHover : "transparent"
    }

    contentItem: Item {
        Row {
            id: row
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.lg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.md

            // A fixed slot, so labels line up whether or not an item has a mark.
            Item {
                width: 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter

                Icon {
                    anchors.centerIn: parent
                    visible: item.checkable && item.checked
                    name: "check"
                    size: 14
                    color: Theme.color.accentText
                }
                Icon {
                    anchors.centerIn: parent
                    visible: !item.checkable && item.iconName !== ""
                    name: item.iconName
                    size: 15
                    color: item._tint
                }
            }

            AppText {
                anchors.verticalCenter: parent.verticalCenter
                text: item.text
                textFormat: Text.PlainText
                color: item._tint
                font.family: Theme.type.sans
                font.pixelSize: Theme.type.bodySize
            }
        }

        AppText {
            id: shortcutLabel
            anchors.right: parent.right
            anchors.rightMargin: Theme.space.lg
            anchors.verticalCenter: parent.verticalCenter
            text: item.shortcutText
            textFormat: Text.PlainText
            color: Theme.color.textTertiary
            font.family: Theme.type.mono
            font.pixelSize: Theme.type.captionSize
        }
    }
}
