import QtQuick
import Quickshell
import qs.Commons
import qs.Ui
import "peri"

// Developer preview: Omarchy's real Commons and Ui modules (linked in) around the adapter's
// LightingControls, in a small window, reading the real daemon. It never writes to the keyboard
// unless someone clicks a control. Run: quickshell -p tests/qml/omarchy-preview
// PERI_PREVIEW_OUT=<file.png>
// renders just the controls to that file and quits. It refuses to run unless QT_QPA_PLATFORM is
// "offscreen", so no window can ever appear on the desktop (never screenshot the screen either).
// One-time setup, links to the real Omarchy modules (machine-specific, so git ignores them):
//   cd tests/qml/omarchy-preview && for d in Commons Ui services; do ln -s /usr/share/omarchy/shell/$d .; done
ShellRoot {
  PeriLighting { id: lighting; interval: 0 }
  // PERI_PREVIEW_PERI: a peri to run instead (e.g. a wrapper that logs what the panel asks for).
  PeriMic {
    id: mic
    periPath: Quickshell.env("PERI_PREVIEW_PERI") !== "" ? Quickshell.env("PERI_PREVIEW_PERI")
                                                         : Quickshell.env("HOME") + "/.local/bin/peri"
  }

  PeriDevices { id: devices; interval: 0 }

  FloatingWindow {
    id: win
    title: "peri-preview"
    visible: Quickshell.env("QT_QPA_PLATFORM") === "offscreen" // never on a real desktop
    implicitWidth: 1200
    implicitHeight: 760
    color: Color.background

    PanelContent {
      id: controls
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.top: parent.top
      anchors.margins: 12
      light: lighting
      mic: mic
      selectedKey: Quickshell.env("PERI_PREVIEW_SELECT") // a device's group, to show its pane
      devs: devices
    }
  }

  Component.onCompleted: {
    if (Quickshell.env("QT_QPA_PLATFORM") !== "offscreen") {
      console.log("refusing to open a window: run with QT_QPA_PLATFORM=offscreen")
      Qt.quit()
      return
    }
    lighting.refresh()
    devices.refresh()
  }

  Timer {
    interval: 3000
    running: Quickshell.env("PERI_PREVIEW_OUT") !== ""
    onTriggered: controls.grabToImage(function(result) {
      console.log("saved " + result.saveToFile(Quickshell.env("PERI_PREVIEW_OUT")))
      Qt.quit()
    })
  }
  Timer { interval: 25000; running: true; onTriggered: Qt.quit() }
}
