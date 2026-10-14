import QtQuick
import qs.Commons
import qs.Ui
import "peri/mic.js" as Mic

// The microphone's controls, in Omarchy's look: what its own controls last reported (mute, dial),
// brightness, colour swatches and effect buttons. No logic of its own: values come from, and
// changes go to, the generic core (PeriMic, passed in as `peri`) and mic.js, which are tested.
//
// Two rules are built into how this is wired:
//   - The slider is connected to `onReleased` only, never `onMoved`, and nothing is bound to a
//     write. A refresh that changes `value` cannot send anything.
//   - The highlighted effect button and colour swatch are what peri last SENT the microphone,
//     from the daemon's record: the microphone cannot report its effect or colour, so this is
//     not what it is showing, and a change made any other way does not appear.
Column {
  id: root

  required property var peri
  property QtObject bar: null
  property string moduleId: ""   // the installed module that drives this mic
  property bool active: true     // the panel is open: only then is the mic read every 2 s

  spacing: Style.space(10)

  // Keep the core pointed at this mic and follow its own controls while the pane is shown.
  Binding { target: root.peri; property: "moduleId"; value: root.moduleId }
  Timer {
    interval: 2000
    running: root.active && root.visible && root.moduleId !== ""
    repeat: true
    triggeredOnStart: true
    onTriggered: root.peri.refresh()
  }

  component Caption: Text {
    width: parent ? parent.width : implicitWidth
    wrapMode: Text.WordWrap
    color: Color.muted
    font.family: Style.font.family
    font.pixelSize: Style.font.caption
  }

  component Label: Text {
    color: Color.foreground
    font.family: Style.font.family
    font.pixelSize: Style.font.body
  }

  Caption {
    visible: !root.peri.online
    text: "The microphone is not answering. Is it plugged in and is the peri daemon running?"
  }

  Row {
    id: body
    visible: root.peri.online
    width: parent.width
    spacing: Style.space(24)

    // --- left: what the mic reports, and brightness ---------------------------------------
    Column {
      id: leftCol
      width: (body.width - body.spacing) / 2
      spacing: Style.space(8)

      Label { text: Mic.muteText(root.peri.mute) }
      Caption { text: Mic.dialText(root.peri.dial) }

      Label { text: "Brightness  " + root.peri.brightness + "%" }
      PanelSlider {
        width: parent.width
        bar: root.bar
        minimum: 0
        maximum: 100
        step: 1
        integer: true
        enabled: !root.peri.busy
        opacity: enabled ? 1 : 0.4
        value: root.peri.brightness
        // Released only: this is the one place a brightness write starts. The core waits for a
        // moment of quiet and sends one set, so a scrolled wheel is not a burst of writes.
        onReleased: function(v) { root.peri.setBrightness(v) }
      }
    }

    // --- right: colour and effects --------------------------------------------------------
    Column {
      id: rightCol
      width: leftCol.width
      spacing: Style.space(8)

      Label { text: "Colour" }
      Flow {
        width: parent.width
        spacing: Style.space(8)
        Repeater {
          model: Mic.presets
          delegate: Rectangle {
            required property var modelData
            width: Style.space(34)
            height: Style.space(34)
            radius: Style.space(8)
            color: "#" + modelData.hex
            readonly property bool last: Mic.swatchSelected(modelData.hex, root.peri.last)
            border.width: last ? 3 : 1
            border.color: last ? Color.foreground : Color.muted
            opacity: root.peri.busy ? 0.4 : 1
            MouseArea {
              anchors.fill: parent
              enabled: !root.peri.busy
              cursorShape: Qt.PointingHandCursor
              onClicked: root.peri.setEffect("static", modelData.hex)
            }
          }
        }
      }

      Label { text: "Effect" }
      Row {
        id: effectRow
        width: parent.width
        spacing: Style.space(6)
        Repeater {
          model: Mic.effects.filter(function(e) { return !e.needsColor })
          delegate: Button {
            required property var modelData
            width: (effectRow.width - effectRow.spacing * 2) / 3
            enabled: !root.peri.busy
            selected: Mic.effectSelected(modelData.id, root.peri.last)
            text: modelData.label
            onClicked: root.peri.setEffect(modelData.id, "")
          }
        }
      }

      Caption { text: Mic.lastSetText(root.peri.last) }
      Caption {
        text: "The microphone cannot report its colour or effect, so the highlight is what peri "
          + "last sent it, and a change made any other way does not show. A change may be kept "
          + "in its own memory, so brightness is sent when you let go of the slider, and only if "
          + "the value changed."
      }
    }
  }

  Text {
    visible: root.peri.error !== ""
    width: parent.width
    wrapMode: Text.WordWrap
    text: root.peri.error
    color: Color.urgent
    font.family: Style.font.family
    font.pixelSize: Style.font.caption
  }
}
