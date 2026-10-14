import QtQuick
import qs.Commons
import qs.Ui
import "peri/mic.js" as Mic

// The microphone's controls, in Omarchy's look. The pane edits two saved looks, Unmuted and Muted:
// a switch picks the look, and the colour, effect and brightness controls change that look's saved
// settings. No logic of its own: values come from, and changes go to, the generic core (PeriMic,
// passed in as `peri`) and mic.js, which are tested.
//
// Two rules are built into how this is wired:
//   - Editing only SAVES. The slider is connected to `onReleased` only (never `onMoved`, which a
//     scrolled wheel would repeat), and nothing here reaches the microphone except the Apply
//     button, because a write to the microphone may be kept in its own memory.
//   - The highlights are the settings saved in the look being edited. The microphone cannot report
//     its effect or colour, so they are not what it is showing, and the pane does not follow its
//     mute state: Apply sends the chosen look whatever that state is.
Column {
  id: root

  required property var peri
  property QtObject bar: null
  property string moduleId: ""   // the installed module that drives this mic
  property bool active: true     // the panel is open: the microphone is read when that becomes true

  spacing: Style.space(10)

  // Keep the core pointed at this mic, and read it once whenever the pane opens.
  Binding { target: root.peri; property: "moduleId"; value: root.moduleId }
  Timer {
    interval: 1
    running: root.active && root.visible && root.moduleId !== ""
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
    text: "The microphone is not answering. Is it plugged in and is the peri daemon running? "
      + "You can still edit the saved looks."
  }

  // --- which look is being edited -----------------------------------------------------------
  // Two normal-sized segments, outlined so both read as buttons and not only the chosen one.
  Row {
    id: lookRow
    spacing: Style.space(6)
    Repeater {
      model: Mic.looks
      delegate: Button {
        required property var modelData
        width: Style.space(170)
        bordered: true
        selected: root.peri.editing === modelData.id
        text: modelData.label
        onClicked: root.peri.selectLook(modelData.id)
      }
    }
  }

  Row {
    id: body
    width: parent.width
    spacing: Style.space(24)

    // --- left: brightness, and what is saved --------------------------------------------------
    Column {
      id: leftCol
      width: (body.width - body.spacing) / 2
      spacing: Style.space(8)

      // The slider shows this look's saved brightness, else the microphone's own (not saved).
      readonly property bool saved: root.peri.look.brightness >= 0
      readonly property int percent: Mic.brightnessPercent(saved ? root.peri.look.brightness
                                                                  : root.peri.deviceBrightness)

      Label { text: "Brightness  " + leftCol.percent + "%" + (leftCol.saved ? "" : "  (not saved)") }
      PanelSlider {
        width: parent.width
        bar: root.bar
        minimum: 0
        maximum: 100
        step: 1
        integer: true
        value: leftCol.percent
        // Released only: this saves the look. It never reaches the microphone.
        onReleased: function(v) { root.peri.chooseBrightness(v) }
      }
      Caption { text: Mic.lookText(root.peri.look) }
    }

    // --- right: colour and effects ------------------------------------------------------------
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
            readonly property bool saved: Mic.swatchSelected(modelData.hex, root.peri.look)
            border.width: saved ? 3 : 1
            border.color: saved ? Color.foreground : Color.muted
            MouseArea {
              anchors.fill: parent
              cursorShape: Qt.PointingHandCursor
              onClicked: root.peri.chooseEffect("static", modelData.hex)
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
            bordered: true
            selected: Mic.effectSelected(modelData.id, root.peri.look)
            text: modelData.label
            onClicked: root.peri.chooseEffect(modelData.id, "")
          }
        }
      }
    }
  }

  // --- Apply: the only thing here that writes to the microphone ----------------------------
  Row {
    width: parent.width
    spacing: Style.space(12)

    Button {
      id: applyButton
      width: Style.space(170)
      bordered: true
      enabled: root.peri.canApply
      opacity: enabled ? 1 : 0.4
      text: "Apply"
      onClicked: root.peri.apply()
    }
    Caption {
      width: parent.width - applyButton.width - parent.spacing
      anchors.verticalCenter: parent.verticalCenter
      text: Mic.applyText(root.peri.editing)
    }
  }

  // --- audio: live sound-card levels, applied as soon as a slider is let go ------------------
  Label { text: "Audio" }
  Row {
    id: audioRow
    width: parent.width
    spacing: Style.space(24)
    readonly property real colWidth: (width - spacing * 2) / 3

    Column {
      width: audioRow.colWidth
      spacing: Style.space(6)
      Label { text: "Gain  " + (root.peri.audio.gain >= 0 ? root.peri.audio.gain + "%" : "unknown") }
      PanelSlider {
        width: parent.width
        bar: root.bar
        minimum: 0
        maximum: 100
        step: 1
        integer: true
        enabled: root.peri.audio.gain >= 0
        opacity: enabled ? 1 : 0.4
        value: Math.max(0, root.peri.audio.gain)
        onReleased: function(v) { root.peri.chooseAudio("gain", v) }
      }
    }
    Column {
      width: audioRow.colWidth
      spacing: Style.space(6)
      Label { text: "Monitor mix  " + (root.peri.audio.monitor >= 0 ? root.peri.audio.monitor + "%" : "unknown") }
      PanelSlider {
        width: parent.width
        bar: root.bar
        minimum: 0
        maximum: 100
        step: 1
        integer: true
        enabled: root.peri.audio.monitor >= 0
        opacity: enabled ? 1 : 0.4
        value: Math.max(0, root.peri.audio.monitor)
        onReleased: function(v) { root.peri.chooseAudio("monitor", v) }
      }
      Button {
        bordered: true
        enabled: root.peri.audio.monitorOn !== null
        selected: root.peri.audio.monitorOn === true
        text: root.peri.audio.monitorOn === true ? "Monitor on" : "Monitor off"
        onClicked: root.peri.chooseAudio("monitor_on", root.peri.audio.monitorOn !== true)
      }
    }
    Column {
      width: audioRow.colWidth
      spacing: Style.space(6)
      Label { text: "Headphones  " + (root.peri.audio.headphones >= 0 ? root.peri.audio.headphones + "%" : "unknown") }
      PanelSlider {
        width: parent.width
        bar: root.bar
        minimum: 0
        maximum: 100
        step: 1
        integer: true
        enabled: root.peri.audio.headphones >= 0
        opacity: enabled ? 1 : 0.4
        value: Math.max(0, root.peri.audio.headphones)
        onReleased: function(v) { root.peri.chooseAudio("headphones", v) }
      }
    }
  }
  Caption {
    text: "These are the sound card's own levels. They apply as soon as you let go of a slider. "
      + "The dial on the microphone has its own setting, and how it relates to these is not known."
  }

  Caption {
    visible: root.peri.notice !== ""
    text: root.peri.notice
  }

  Caption {
    text: "Editing a look only saves it. The microphone cannot report its colour or effect, so "
      + "the highlights are your saved settings, not what it is showing. Apply sends the look, and "
      + "the microphone may keep it in its own memory."
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
