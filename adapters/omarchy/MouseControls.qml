import QtQuick
import qs.Commons
import qs.Ui
import "peri/mouse.js" as Mouse

// The DeathAdder's controls, in Omarchy's look: battery, DPI and poll rate. No logic of its own:
// values come from, and changes go to, the generic core (PeriMouse, passed in as `peri`) and
// mouse.js, which are tested.
//
// Writes may be kept in the mouse's own memory, so the slider is connected to `onReleased` only
// (never `onMoved`: a scrolled wheel would repeat it) and the core sends once after a pause.
Column {
  id: root

  required property var peri
  property QtObject bar: null
  property string moduleId: ""   // the installed module that drives this mouse
  property bool active: true     // the panel is open: the mouse is read when that becomes true

  spacing: Style.space(10)

  // Keep the core pointed at this mouse, and read it once whenever the pane opens.
  Binding { target: root.peri; property: "moduleId"; value: root.moduleId }
  Timer {
    interval: 1
    running: root.active && root.visible && root.moduleId !== ""
    onTriggered: root.peri.refresh("the pane opened")
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
    visible: !root.peri.online && !root.peri.busy
    text: "The mouse is not answering. It may be asleep: click it, then press Refresh."
  }
  Caption {
    visible: root.peri.error !== ""
    text: root.peri.error
  }

  Label { text: Mouse.batteryText(root.peri.info) }

  Label { text: Mouse.dpiText(root.peri.info) }
  PanelSlider {
    width: parent.width
    bar: root.bar
    enabled: root.peri.online
    minimum: Mouse.minDpi
    maximum: Mouse.maxDpi
    step: 50
    integer: true
    value: root.peri.info.dpi !== null ? root.peri.info.dpi.x : Mouse.minDpi
    // Released only; the core waits for a pause before it sends.
    onReleased: function(v) { root.peri.chooseDpi(v) }
  }
  Flow {
    width: parent.width
    spacing: Style.space(6)
    Repeater {
      model: Mouse.dpiPresets
      delegate: Button {
        required property var modelData
        bordered: true
        enabled: root.peri.online
        selected: Mouse.dpiSelected(modelData, root.peri.info)
        text: String(modelData)
        onClicked: root.peri.chooseDpi(modelData)
      }
    }
  }

  Label { text: "Poll rate" }
  Row {
    spacing: Style.space(6)
    Repeater {
      model: Mouse.pollRates
      delegate: Button {
        required property var modelData
        bordered: true
        enabled: root.peri.online
        selected: Mouse.pollSelected(modelData, root.peri.info)
        text: modelData + " Hz"
        onClicked: root.peri.choosePoll(modelData)
      }
    }
  }

  Button {
    bordered: true
    enabled: !root.peri.busy
    text: "Refresh"
    onClicked: root.peri.refresh("the Refresh button")
  }
  Caption { text: "Changes may be kept in the mouse's own memory." }
}
