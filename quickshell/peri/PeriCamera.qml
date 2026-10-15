import QtQuick
import Quickshell
import Quickshell.Io
import "camera.js" as C
import "cli.js" as Cli

// A webcam (the Logitech BRIO) as seen by the peri daemon. Like PeriMouse it imports nothing from the
// host shell and draws nothing; hosts bind to the properties below.
//
//   PeriCamera { id: cam; moduleId: "brio" }
//   MySlider { onReleased: cam.chooseValue("brightness", v) }
//
// Reads happen when refresh() is called, which a host does when it opens the pane, not on a timer. A
// control's inactive state depends on the others (exposure time is held while exposure is automatic), so
// every write is followed by one more read. Writes: the camera may keep a setting, a slider's released
// signal fires once per wheel notch, so chooseValue() waits for a pause; an unchanged value is never sent,
// and only one write runs at a time.
Scope {
  id: root

  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property string moduleId: ""

  // Off until the first writes of each kind of control have been tried on the real camera (brightness,
  // an integer, has; switches, menus and the stepped pan and tilt have not). While false nothing is sent,
  // and the pane shows its controls as unavailable.
  property bool writable: false

  readonly property bool online: state.s.online
  readonly property var controls: state.s.controls
  readonly property var rows: C.rows(state.s.controls)
  readonly property var columns: C.columns(C.rows(state.s.controls))
  readonly property bool busy: reader.running || writer.running
  readonly property string error: state.error

  function refresh(why) {
    if (root.moduleId === "" || reader.running || writer.running) return
    state.error = ""
    reader.running = true
  }

  // A slider's value, or a button's: validated against what the camera listed, then sent after 600 ms of
  // quiet (only the last choice counts). Switches and menu buttons use choose() and send at once.
  function chooseValue(name, v) {
    var c = C.byName(state.s.controls, name)
    if (!root.writable || !c) return
    var next = C.snap(c, v)
    if (C.isCurrent(c, next)) { state.pendingName = ""; return }
    state.pendingName = name
    state.pendingValue = next
    pause.restart()
  }

  function choose(name, v) {
    var c = C.byName(state.s.controls, name)
    if (!root.writable || !c || C.isCurrent(c, v)) return
    send(C.setArg(c, v))
  }

  function send(arg) {
    if (arg === "" || !root.writable || root.moduleId === "" || writer.running || reader.running) return
    state.error = ""
    writer.command = Cli.command(root.periPath, ["set", root.moduleId, arg])
    writer.running = true
  }

  QtObject {
    id: state
    property var s: C.parseControls("")
    property string error: ""
    property string pendingName: ""
    property int pendingValue: 0
  }

  Timer {
    id: pause
    interval: 600
    onTriggered: {
      if (reader.running || writer.running) { // busy: try again, never drop the choice
        pause.restart()
        return
      }
      var c = C.byName(state.s.controls, state.pendingName)
      var v = state.pendingValue
      state.pendingName = ""
      if (c) root.send(C.setArg(c, v))
    }
  }

  Process {
    id: reader
    command: Cli.command(root.periPath, ["controls", root.moduleId])
    stdout: StdioCollector {
      onStreamFinished: state.s = C.parseControls(this.text)
    }
    stderr: StdioCollector {
      onStreamFinished: { if (this.text.trim() !== "") state.error = this.text.trim() }
    }
    onExited: function(exitCode) {
      if (exitCode !== 0) state.s = C.parseControls("")
    }
  }

  // One more read after a write, because a control's inactive state depends on the others. It waits
  // until neither process is running, so the read is never skipped by refresh()'s own guard.
  Timer {
    id: after
    interval: 250
    onTriggered: {
      if (reader.running || writer.running) { after.restart(); return }
      root.refresh("after a write")
    }
  }

  Process {
    id: writer
    stdout: StdioCollector {
      onStreamFinished: {
        var r = C.parseSet(this.text)
        if (!r.ok) return
        // Show the camera's own value at once, then read everything again (the timer above): other
        // controls may have become active or inactive.
        after.restart()
        state.s = {
          online: state.s.online,
          controls: state.s.controls.map(function(c) {
            if (c.name !== r.name || r.value === null) return c
            return Object.assign({}, c, { value: r.value })
          })
        }
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { if (this.text.trim() !== "") state.error = this.text.trim() }
    }
  }
}
