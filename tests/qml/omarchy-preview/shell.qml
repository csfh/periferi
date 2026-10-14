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

  // PERI_PREVIEW_DEBUG=1: switch the debug log on and press a sequence of controls, to see what the log
  // says. The sequence CHANGES SAVED LOOKS and the Follow mute setting, so it runs only if both
  // PERI_PREVIEW_PERI (a peri that talks to a throwaway daemon with its own config directory) and
  // PERI_PREVIEW_ISOLATED=1 are set; otherwise it does nothing. Its Apply step is guarded so it
  // can only ever be the "nothing to send" refusal: it is never allowed to reach the microphone.
  Timer {
    id: script
    property int step: 0
    interval: 400
    repeat: true
    running: Quickshell.env("PERI_PREVIEW_DEBUG") === "1" && Quickshell.env("PERI_PREVIEW_ISOLATED") === "1"
             && Quickshell.env("PERI_PREVIEW_PERI") !== ""
    onTriggered: {
      var steps = [
        function() { mic.setDebug(true) },
        function() {}, function() {}, function() {}, function() {},
        function() { mic.selectLook("muted") },
        function() { mic.chooseEffect("breathing") },
        function() { mic.chooseColor(1, "ff0000") },
        function() { mic.chooseColor(2, "0000ff") },
        function() { mic.chooseEffect("wave") },
        function() { mic.chooseSpeed("fast") },
        function() { mic.chooseColor(2, "ff0000") },
        function() { mic.chooseBrightness(40) },
        function() { mic.chooseBrightness(40) },
        function() { mic.chooseRandomColors() },
        function() { mic.setFollow(false) },
        function() { mic.selectLook("unmuted") },
        function() { if (!mic.canApply) mic.apply() },
        function() { mic.setDebug(false) }
      ]
      if (step < steps.length) steps[step++]()
      else running = false
    }
  }
}
