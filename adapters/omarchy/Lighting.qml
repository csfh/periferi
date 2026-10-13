import QtQuick
import qs.Commons
import qs.Ui
import "peri"

// Omarchy bar widget: a keyboard glyph tinted with the keyboard's lighting. Click it for the
// device menu: your devices with a module, the modules you can install, and the devices
// nothing can drive yet. All the data work lives in the generic core (./peri); this file is
// only how it looks in Omarchy.
BarWidget {
  id: root
  moduleName: "peri.lighting"

  property bool popupOpen: false
  property string expandedKey: ""   // the one device row that is open
  property bool availableOpen: false
  property bool withoutOpen: false
  function close() { popupOpen = false } // PopupCard calls this on an outside click

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  PeriLighting { id: peri }
  PeriDevices { id: devices }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: "󰌌" // nf-md-keyboard
    active: peri.online && peri.lit
    activeColor: peri.tint
    tooltipText: !peri.online ? "Keyboard lighting: daemon or keyboard not reachable"
      : !peri.lit ? "Keyboard lighting is off"
      : "Keyboard lighting: " + peri.effect.replace(/_/g, " ") + ", " + peri.brightness + "%"
    onPressed: function(b) {
      peri.refresh()
      devices.refresh()
      root.popupOpen = !root.popupOpen
    }
  }

  PopupCard {
    id: popup
    anchorItem: root
    owner: root
    bar: root.bar
    open: root.popupOpen
    contentWidth: popup.fittedContentWidth(Style.space(300))
    contentHeight: popup.fittedContentHeight(column.implicitHeight)

    Column {
      id: column
      anchors.fill: parent
      spacing: Style.space(6)

      Text {
        visible: !devices.online
        width: parent.width
        wrapMode: Text.WordWrap
        text: "The peri daemon is not reachable (" + (devices.error || "starting") + ")"
        color: Color.urgent
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
      }
      Text {
        visible: devices.actionError !== ""
        width: parent.width
        wrapMode: Text.WordWrap
        text: devices.actionError
        color: Color.urgent
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
      }

      // --- Devices with an installed module -------------------------------------------
      PanelSectionHeader { text: "DEVICES" }

      Text {
        visible: devices.managed.length === 0
        width: parent.width
        wrapMode: Text.WordWrap
        text: "No device has a module installed. Install one below."
        color: Color.muted
        font.family: Style.font.family
        font.pixelSize: Style.font.caption
      }

      Repeater {
        model: devices.managed

        delegate: Column {
          id: row
          required property var modelData
          readonly property bool open: root.expandedKey === modelData.key
          width: column.width
          spacing: Style.space(4)

          Button {
            width: parent.width
            leftAlign: true
            text: (row.open ? "▾  " : "▸  ") + row.modelData.name
              + (row.modelData.connected ? "" : "  (not connected)")
            onClicked: root.expandedKey = row.open ? "" : row.modelData.key
          }

          Column {
            visible: row.open
            width: parent.width
            spacing: Style.space(4)

            // The existing lighting control: the keyboard's on/off.
            Toggle {
              visible: row.modelData.connected && peri.online
              width: parent.width
              label: "Lighting"
              description: peri.lit ? peri.effect.replace(/_/g, " ") + ", " + peri.brightness + "%" : "Off"
              checked: peri.lit
              onClicked: peri.set(["enable=" + (peri.lit ? "0" : "1")])
            }

            Text {
              visible: row.modelData.setup
              width: parent.width
              wrapMode: Text.WordWrap
              text: "Run `peri setup` in a terminal to update the device rule."
              color: Color.muted
              font.family: Style.font.family
              font.pixelSize: Style.font.caption
            }

            Button {
              width: parent.width
              leftAlign: true
              opacity: devices.busy ? 0.5 : 1
              text: "Remove module"
              onClicked: if (!devices.busy) devices.remove(row.modelData.module)
            }
          }
        }
      }

      PanelSeparator {}

      // --- Modules that can be installed -----------------------------------------------
      Button {
        width: parent.width
        leftAlign: true
        text: (root.availableOpen ? "▾  " : "▸  ") + "Available modules (" + devices.installable.length + ")"
        onClicked: root.availableOpen = !root.availableOpen
      }

      Column {
        visible: root.availableOpen
        width: parent.width
        spacing: Style.space(4)

        Text {
          visible: devices.installable.length === 0
          width: parent.width
          wrapMode: Text.WordWrap
          text: "Every bundled module is installed."
          color: Color.muted
          font.family: Style.font.family
          font.pixelSize: Style.font.caption
        }

        Repeater {
          model: devices.installable

          delegate: Item {
            id: mod
            required property var modelData
            width: column.width
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
              opacity: devices.busy ? 0.5 : 1
              text: "Install"
              onClicked: if (!devices.busy) devices.install(mod.modelData.id)
            }
          }
        }
      }

      // --- Devices nothing can drive ---------------------------------------------------
      Button {
        width: parent.width
        leftAlign: true
        text: (root.withoutOpen ? "▾  " : "▸  ") + "Devices without a module (" + devices.unmanaged.length + ")"
        onClicked: root.withoutOpen = !root.withoutOpen
      }

      Column {
        visible: root.withoutOpen
        width: parent.width
        spacing: Style.space(4)

        Text {
          visible: devices.unmanaged.length === 0
          width: parent.width
          wrapMode: Text.WordWrap
          text: "Nothing else is connected."
          color: Color.muted
          font.family: Style.font.family
          font.pixelSize: Style.font.caption
        }

        Repeater {
          model: devices.unmanaged

          delegate: Column {
            id: other
            required property var modelData
            width: column.width

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
  }
}
