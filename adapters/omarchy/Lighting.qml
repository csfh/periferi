import QtQuick
import qs.Commons
import qs.Ui
import "peri"
import "centering.js" as Center

// Omarchy bar widget: a keyboard glyph tinted with the keyboard's lighting. Click it for a
// panel in two panes, like Omarchy's own power and network panels (see PanelContent.qml).
// All the data work lives in the generic core (./peri); this file is only how it looks in Omarchy.
Panel {
  id: root
  moduleName: "peri.lighting"
  ipcTarget: "peri.lighting"

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  PeriLighting { id: lightingState }
  PeriMic { id: micState }
  PeriDevices { id: devicesState }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: "󰌌" // nf-md-keyboard
    active: lightingState.online && lightingState.lit
    activeColor: lightingState.tint
    tooltipText: !lightingState.online ? "Keyboard lighting: daemon or keyboard not reachable"
      : !lightingState.lit ? "Keyboard lighting is off"
      : "Keyboard lighting: " + lightingState.effect.replace(/_/g, " ") + ", " + lightingState.brightness + "%"
    onPressed: function(b) {
      lightingState.refresh()
      devicesState.refresh()
      root.toggle()
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher

    // Centred on the screen both ways, wherever the bar icon is (see centering.js). The size is
    // worked out here, and the gap from it, so the two never depend on each other.
    readonly property bool sideBar: barPos === "left" || barPos === "right"
    centerOnBar: true
    contentWidth: Center.limit(Style.space(1200), screenW, sideBar ? barW : 0, margin)
    contentHeight: Center.limit(content.implicitHeight + verticalContentInset, screenH,
                                sideBar ? 0 : barH, margin)
    gap: sideBar ? Center.centerGap(screenW, contentWidth, barW)
                 : Center.centerGap(screenH, contentHeight, barH)

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      PanelContent {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        light: lightingState
        mic: micState
        devs: devicesState
        bar: root.bar
      }
    }
  }
}
