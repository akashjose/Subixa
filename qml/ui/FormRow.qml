import QtQuick
import QtQuick.Layouts
import Subixa

// label | control | value readout, plus an optional help line under the control.
//
// See SectionCard for why the internal structure goes through `children:`: a
// default property alias would otherwise swallow this file's own children.
RowLayout {
    id: formRow

    property string label: ""
    property string help: ""
    property string readout: ""
    default property alias control: holder.data

    Layout.fillWidth: true
    spacing: Theme.space.lg

    children: [
        ColumnLayout {
            Layout.preferredWidth: 180
            Layout.maximumWidth: 180
            Layout.alignment: Qt.AlignTop
            spacing: Theme.space.xxs

            AppText {
                Layout.fillWidth: true
                text: formRow.label
                textFormat: Text.PlainText
                color: Theme.color.textPrimary
                font.family: Theme.type.sans
                font.pixelSize: Theme.type.bodySize
                wrapMode: Text.WordWrap
            }

            AppText {
                Layout.fillWidth: true
                visible: formRow.help !== ""
                text: formRow.help
                textFormat: Text.PlainText
                color: Theme.color.textTertiary
                font.family: Theme.type.sans
                font.pixelSize: Theme.type.captionSize
                wrapMode: Text.WordWrap
            }
        },

        RowLayout {
            id: holder
            Layout.fillWidth: true
            Layout.maximumWidth: 340
            Layout.alignment: Qt.AlignTop
            spacing: Theme.space.md
        },

        AppText {
            Layout.preferredWidth: 64
            Layout.alignment: Qt.AlignTop
            horizontalAlignment: Text.AlignRight
            visible: formRow.readout !== ""
            text: formRow.readout
            textFormat: Text.PlainText
            color: Theme.color.textSecondary
            font.family: Theme.type.mono
            font.pixelSize: Theme.type.monoSize
        }
    ]
}
