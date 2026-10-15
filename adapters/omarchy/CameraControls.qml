import QtQuick
import qs.Commons
import qs.Ui
import "peri/camera.js" as Cam

// A camera's controls, in Omarchy's look, drawn from the list the camera reports (a slider for a range, a
// pair of buttons for a switch, a button per option for a menu). No logic of its own: values come from, and
// changes go to, the generic core (PeriCamera, passed in as `peri`) and camera.js, which are tested.
//
// Writes may be kept by the camera, so a slider is connected to `onReleased` only (never `onMoved`: a
// scrolled wheel would repeat it) and the core sends once after a pause. A control the camera holds
// inactive (exposure time while exposure is automatic) is greyed out.
Column {
  id: root

  required property var peri
  property QtObject bar: null
  property string moduleId: ""   // the installed module that drives this camera
  property bool active: true     // the panel is open: the camera is read when that becomes true

  spacing: Style.space(8)

  // Keep the core pointed at this camera, and read it once whenever the pane opens.
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
    text: "The camera is not answering. Is it plugged in and is the peri daemon running?"
  }
  Caption {
    visible: root.peri.error !== ""
    text: root.peri.error
  }

  // One row of the pane: a section header, a slider, a switch or a menu, as the control's type says.
  component ControlRow: Loader {
    id: row
    required property var modelData

      sourceComponent: modelData.kind === "header" ? header
        : modelData.control.type === "int" ? slider
        : modelData.control.type === "bool" ? toggle : menu

      Component {
        id: header
        Label { text: row.modelData.title; font.bold: true; topPadding: Style.space(6) }
      }

      Component {
        id: slider
        Column {
          width: row.width
          spacing: Style.space(4)
          readonly property var c: row.modelData.control
          Label { text: parent.c.title + "  " + Cam.valueText(parent.c) }
          PanelSlider {
            width: parent.width
            bar: root.bar
            enabled: root.peri.online && root.peri.writable && !parent.c.inactive
            minimum: parent.c.min
            maximum: parent.c.max
            step: parent.c.step
            integer: true
            value: parent.c.value
            // Released only; the core waits for a pause before it sends.
            onReleased: function(v) { root.peri.chooseValue(parent.c.name, v) }
          }
        }
      }

      Component {
        id: toggle
        Row {
          spacing: Style.space(8)
          readonly property var c: row.modelData.control
          Label { text: parent.c.title; width: Style.space(240); anchors.verticalCenter: parent.verticalCenter }
          Repeater {
            model: [{ label: "Off", v: 0 }, { label: "On", v: 1 }]
            delegate: Button {
              required property var modelData
              bordered: true
              enabled: root.peri.online && root.peri.writable && !row.modelData.control.inactive
              selected: row.modelData.control.value === modelData.v
              text: modelData.label
              onClicked: root.peri.choose(row.modelData.control.name, modelData.v)
            }
          }
        }
      }

      Component {
        id: menu
        Column {
          width: row.width
          spacing: Style.space(4)
          readonly property var c: row.modelData.control
          Label { text: parent.c.title }
          Flow {
            width: parent.width
            spacing: Style.space(6)
            Repeater {
              model: row.modelData.control.options
              delegate: Button {
                required property var modelData
                bordered: true
                enabled: root.peri.online && root.peri.writable && !row.modelData.control.inactive
                selected: row.modelData.control.value === modelData.index
                text: modelData.label
                onClicked: root.peri.choose(row.modelData.control.name, modelData.index)
              }
            }
          }
        }
      }
  }

  Row {
    width: parent.width
    spacing: Style.space(24)
    Repeater {
      model: root.peri.columns
      delegate: Column {
        required property var modelData
        width: (root.width - Style.space(24)) / 2
        spacing: Style.space(8)
        Repeater {
          model: modelData
          delegate: ControlRow { width: parent.width }
        }
      }
    }
  }

  Button {
    bordered: true
    enabled: !root.peri.busy
    text: "Refresh"
    onClicked: root.peri.refresh("the Refresh button")
  }
  Caption {
    text: root.peri.writable ? "Changes may be kept by the camera."
                             : "Changing settings is switched off until each kind of control has been tried on the camera."
  }
}
