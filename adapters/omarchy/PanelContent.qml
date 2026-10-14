import QtQuick
import qs.Commons
import qs.Ui
import "peri/menu.js" as Menu

// The two panes of the Peripherals panel: every device on the left, the selected device's
// controls on the right. A plain Item, so it can also be drawn on its own (see
// tests/qml/omarchy-preview). `light` and `devs` are the generic core's PeriLighting and
// PeriDevices (and `mic`, PeriMic, for a microphone); nothing here computes anything, it only lays them out.
Item {
  id: content

  required property var light
  required property var mic
  property var mouse: null          // PeriMouse, for a mouse; hosts without one leave it out
  required property var devs
  property QtObject bar: null
  property bool open: true          // whether the panel is showing; the mic is only polled while it is

  property string selectedKey: ""   // the managed device shown on the right
  property bool availableOpen: false
  property bool withoutOpen: false

  // The chosen device, else the first one; null when no device has a module.
  readonly property var selected: Menu.selectEntry(devs.managed, selectedKey)

  implicitHeight: Math.max(devicesPane.implicitHeight, detailsPane.implicitHeight)

  component Note: Text {
    width: parent ? parent.width : implicitWidth
    wrapMode: Text.WordWrap
    color: Color.muted
    font.family: Style.font.family
    font.pixelSize: Style.font.caption
  }

  // ============================ left: every device ============================
  Column {
    id: devicesPane
    anchors.left: parent.left
    anchors.top: parent.top
    width: Style.space(250)
    spacing: Style.space(6)

    Text {
      visible: !devs.online
      width: parent.width
      wrapMode: Text.WordWrap
      text: "The peri daemon is not reachable (" + (devs.error || "starting") + ")"
      color: Color.urgent
      font.family: Style.font.family
      font.pixelSize: Style.font.caption
    }
    Text {
      visible: devs.actionError !== ""
      width: parent.width
      wrapMode: Text.WordWrap
      text: devs.actionError
      color: Color.urgent
      font.family: Style.font.family
      font.pixelSize: Style.font.caption
    }

    PanelSectionHeader { text: "DEVICES" }

    Note {
      visible: devs.managed.length === 0
      text: "No device has a module installed. Install one below."
    }

    Repeater {
      model: devs.managed
      delegate: Button {
        required property var modelData
        width: devicesPane.width
        leftAlign: true
        selected: content.selected !== null && content.selected.key === modelData.key
        text: modelData.name + (modelData.connected ? "" : "  (not connected)")
        onClicked: content.selectedKey = modelData.key
      }
    }

    PanelSeparator {}

    // --- modules that can be installed ---
    Button {
      width: parent.width
      leftAlign: true
      text: (content.availableOpen ? "▾  " : "▸  ") + "Available modules (" + devs.installable.length + ")"
      onClicked: content.availableOpen = !content.availableOpen
    }
    Column {
      visible: content.availableOpen
      width: parent.width
      spacing: Style.space(4)

      Note {
        visible: devs.installable.length === 0
        text: "Every bundled module is installed."
      }

      Repeater {
        model: devs.installable
        delegate: Item {
          id: mod
          required property var modelData
          width: devicesPane.width
          height: Math.max(label.implicitHeight, install.implicitHeight)

          Column {
            id: label
            anchors.left: parent.left
            anchors.right: install.left
            anchors.rightMargin: Style.space(8)
            anchors.verticalCenter: parent.verticalCenter

            Text {
              width: parent.width
              elide: Text.ElideRight
              text: mod.modelData.name + "  " + mod.modelData.version
              color: Color.foreground
              font.family: Style.font.family
              font.pixelSize: Style.font.body
            }
            Text {
              width: parent.width
              text: mod.modelData.connected ? "Its device is connected" : "Its device is not connected"
              color: mod.modelData.connected ? Color.accent : Color.muted
              font.family: Style.font.family
              font.pixelSize: Style.font.caption
            }
          }

          Button {
            id: install
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            opacity: devs.busy ? 0.5 : 1
            text: "Install"
            onClicked: if (!devs.busy) devs.install(mod.modelData.id)
          }
        }
      }
    }

    // --- devices nothing can drive ---
    Button {
      width: parent.width
      leftAlign: true
      text: (content.withoutOpen ? "▾  " : "▸  ") + "Devices without a module (" + devs.unmanaged.length + ")"
      onClicked: content.withoutOpen = !content.withoutOpen
    }
    Column {
      visible: content.withoutOpen
      width: parent.width
      spacing: Style.space(4)

      Note {
        visible: devs.unmanaged.length === 0
        text: "Nothing else is connected."
      }

      Repeater {
        model: devs.unmanaged
        delegate: Column {
          id: other
          required property var modelData
          width: devicesPane.width

          Text {
            width: parent.width
            elide: Text.ElideRight
            text: other.modelData.name
            color: Color.foreground
            font.family: Style.font.family
            font.pixelSize: Style.font.body
          }
          Text {
            text: other.modelData.vid + ":" + other.modelData.pid
            color: Color.muted
            font.family: Style.font.family
            font.pixelSize: Style.font.caption
          }
        }
      }
    }
  }

  Rectangle {
    id: divider
    anchors.left: devicesPane.right
    anchors.leftMargin: Style.space(12)
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: 1
    color: Qt.rgba(Color.foreground.r, Color.foreground.g, Color.foreground.b, 0.12)
  }

  // ======================== right: the selected device ========================
  Column {
    id: detailsPane
    anchors.left: divider.right
    anchors.leftMargin: Style.space(12)
    anchors.right: parent.right
    anchors.top: parent.top
    spacing: Style.space(10)

    Note {
      visible: content.selected === null
      text: "Pick a device on the left. Devices with an installed module are controlled here."
    }

    // Header: the device, its module, and Remove.
    Item {
      visible: content.selected !== null
      width: parent.width
      height: Math.max(title.implicitHeight, remove.implicitHeight)

      Column {
        id: title
        anchors.left: parent.left
        anchors.right: remove.left
        anchors.rightMargin: Style.space(8)
        anchors.verticalCenter: parent.verticalCenter

        Text {
          width: parent.width
          elide: Text.ElideRight
          text: content.selected ? content.selected.name : ""
          color: Color.foreground
          font.family: Style.font.family
          font.pixelSize: Style.font.subtitle
          font.bold: true
        }
        Text {
          text: content.selected ? "Module " + content.selected.module
            + (content.selected.connected ? "" : "  ·  not connected") : ""
          color: Color.muted
          font.family: Style.font.family
          font.pixelSize: Style.font.caption
        }
      }

      Button {
        id: remove
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        opacity: devs.busy ? 0.5 : 1
        text: "Remove module"
        onClicked: if (!devs.busy && content.selected) devs.remove(content.selected.module)
      }
    }

    Note {
      visible: content.selected !== null && content.selected.setup
      text: "Run `peri setup` in a terminal to update the device rule."
    }
    Note {
      visible: content.selected !== null && !content.selected.connected
      text: "This device is not connected, so there is nothing to control. The module stays installed."
    }
    Note {
      visible: content.selected !== null && content.selected.connected
               && Menu.drivesKeyboard(content.selected) && !light.online
      text: "The keyboard is not answering. Is it plugged in and is the peri daemon running?"
    }
    Note {
      visible: content.selected !== null && content.selected.connected
               && !Menu.drivesKeyboard(content.selected) && content.selected.engine !== "razer"
               && !(content.selected.engine === "razer90" && content.mouse !== null)
      text: "This device has no controls in the panel yet. In a terminal, `peri status " + (content.selected ? content.selected.module : "") + "` reads it."
    }

    MicControls {
      visible: content.selected !== null && content.selected.connected
               && content.selected.engine === "razer"
      width: parent.width
      peri: content.mic
      active: content.open
      bar: content.bar
      moduleId: content.selected && content.selected.engine === "razer" ? content.selected.module : ""
    }

    MouseControls {
      visible: content.selected !== null && content.selected.connected
               && content.selected.engine === "razer90" && content.mouse !== null
      width: parent.width
      peri: content.mouse
      active: content.open
      bar: content.bar
      moduleId: content.selected && content.selected.engine === "razer90" ? content.selected.module : ""
    }

    LightingControls {
      visible: content.selected !== null && content.selected.connected
               && Menu.drivesKeyboard(content.selected) && light.online
      width: parent.width
      peri: light
      bar: content.bar
      moduleId: content.selected ? content.selected.module : ""
    }
  }
}
