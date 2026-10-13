import QtQuick
import Quickshell
import "peri"

// The smallest possible host, to show the core works in plain Quickshell with
// no Omarchy: a small badge in the top-right corner. Run it with
//   quickshell -p adapters/standalone
// Click the badge to refresh. Uses a standard Unicode glyph, so no Nerd Font.
ShellRoot {
  PeriLighting { id: peri }

  PanelWindow {
    anchors { top: true; right: true }
    margins { top: 8; right: 8 }
    implicitWidth: 40
    implicitHeight: 32
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore

    Rectangle {
      anchors.fill: parent
      radius: 6
      color: "#202020"

      Text {
        anchors.centerIn: parent
        text: "⌨" // keyboard
        font.pixelSize: 20
        color: peri.online && peri.lit ? peri.tint : "#777777"
      }
      MouseArea {
        anchors.fill: parent
        onClicked: peri.refresh()
      }
    }
  }
}
