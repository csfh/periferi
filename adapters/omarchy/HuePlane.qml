import QtQuick
import qs.Commons
import "peri/lighting.js" as L

// A hue/saturation picker: hue runs left to right, saturation from white at the bottom to
// full color at the top. Brightness is not on it (the keyboard caps it, and it has its own
// slider). Click or drag to pick; `picked` fires on press, on every move and on release, so
// the last value sent is the one the pointer ended on.
//
// `hue` and `sat` are what the keyboard reports (0-255). While a drag is going the marker
// follows the pointer instead, so it cannot jump back to an older reply.
Item {
  id: root

  property int hue: 0
  property int sat: 0
  signal picked(int hue, int sat)

  property bool dragging: false
  property int dragHue: 0
  property int dragSat: 0

  readonly property int shownHue: dragging ? dragHue : hue
  readonly property int shownSat: dragging ? dragSat : sat

  implicitHeight: Style.space(210)
  height: implicitHeight

  Rectangle {
    id: plane
    anchors.fill: parent
    radius: Style.space(6)
    clip: true
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.25)

    // Hue across, at full saturation. Stops sit at the sixths where the primary colors are.
    gradient: Gradient {
      orientation: Gradient.Horizontal
      GradientStop { position: 0 / 6; color: Qt.hsva(0 / 6, 1, 1, 1) }
      GradientStop { position: 1 / 6; color: Qt.hsva(1 / 6, 1, 1, 1) }
      GradientStop { position: 2 / 6; color: Qt.hsva(2 / 6, 1, 1, 1) }
      GradientStop { position: 3 / 6; color: Qt.hsva(3 / 6, 1, 1, 1) }
      GradientStop { position: 4 / 6; color: Qt.hsva(4 / 6, 1, 1, 1) }
      GradientStop { position: 5 / 6; color: Qt.hsva(5 / 6, 1, 1, 1) }
      GradientStop { position: 6 / 6; color: Qt.hsva(0, 1, 1, 1) }
    }

    // Fade to white going down. White with alpha 0 at the top, not "transparent" (black),
    // so the blend does not turn gray.
    Rectangle {
      anchors.fill: parent
      radius: parent.radius
      gradient: Gradient {
        GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0) }
        GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 1) }
      }
    }
  }

  // The marker: filled with the picked color, ringed so it shows on any background.
  Rectangle {
    id: marker
    readonly property var at: L.hueSatToPlane(root.shownHue, root.shownSat, root.width, root.height)
    width: Style.space(16)
    height: width
    radius: width / 2
    x: at.x - width / 2
    y: at.y - height / 2
    color: Qt.hsva(root.shownHue / 255, root.shownSat / 255, 1, 1)
    border.width: 2
    border.color: "white"

    Rectangle {
      anchors.fill: parent
      anchors.margins: -2
      radius: width / 2
      color: "transparent"
      border.width: 1
      border.color: Qt.rgba(0, 0, 0, 0.55)
    }
  }

  MouseArea {
    anchors.fill: parent
    cursorShape: Qt.CrossCursor

    function pick(mouse) {
      var p = L.planeToHueSat(mouse.x, mouse.y, width, height)
      root.dragHue = p.hue
      root.dragSat = p.sat
      root.picked(p.hue, p.sat)
    }

    onPressed: function(mouse) { root.dragging = true; pick(mouse) }
    onPositionChanged: function(mouse) { if (pressed) pick(mouse) }
    onReleased: function(mouse) { pick(mouse); root.dragging = false }
    onCanceled: root.dragging = false
  }
}
