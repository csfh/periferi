import QtQuick
import Quickshell
import Quickshell.Io
import "mouse.js" as M
import "cli.js" as Cli

// A mouse (the Razer DeathAdder V3 Pro) as seen by the peri daemon. Like PeriMic it imports nothing
// from the host shell and draws nothing; hosts bind to the properties below.
//
//   PeriMouse { id: mouse; moduleId: "razer-deathadder-v3-pro" }
//   MyButton { onClicked: mouse.chooseDpi(1600) }
//
// Reads happen when refresh() is called, which a host does when it opens the pane, not on a timer
// (a sleeping mouse takes seconds to answer each read). Writes: a DPI or poll-rate set may be kept
// in the mouse's own memory, so chooseDpi() waits for a pause (a slider's released signal fires
// once per wheel notch), an unchanged value is never sent, and only one write runs at a time.
Scope {
  id: root

  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property string moduleId: ""

  // Both sets were seen working on the real mouse (2026-10-14, read back from the device). Set
  // this false and no choose*() call sends anything: the pane shows its controls as unavailable.
  property bool writable: true

  readonly property bool online: state.s.online
  readonly property var info: state.s         // what mouse.js parsed from `peri status`
  readonly property bool busy: reader.running || writer.running
  readonly property string error: state.error

  function refresh(why) {
    if (root.moduleId === "" || reader.running || writer.running) return
    state.error = ""
    reader.running = true
  }

  // A DPI chosen on the slider or a preset button: sent after 600 ms of quiet, once.
  function chooseDpi(v) {
    if (!root.writable) return
    var next = M.clampDpi(v)
    if (state.s.online && state.s.dpi !== null && state.s.dpi.x === next && state.s.dpi.y === next) {
      state.pendingDpi = -1
      return
    }
    state.pendingDpi = next
    pause.restart()
  }

  function choosePoll(rate) {
    if (!root.writable) return
    if (state.s.online && state.s.poll === rate) return
    send(M.pollArg(rate))
  }

  function send(arg) {
    if (!root.writable) return
    if (arg === "" || root.moduleId === "" || writer.running || reader.running) return
    state.error = ""
    writer.command = Cli.command(root.periPath, ["set", root.moduleId, arg])
    writer.running = true
  }

  QtObject {
    id: state
    property var s: M.parseStatus("")
    property string error: ""
    property int pendingDpi: -1
  }

  Timer {
    id: pause
    interval: 600
    onTriggered: {
      if (reader.running || writer.running) { // a read or write is in flight: try again, never drop the choice
        pause.restart()
        return
      }
      var v = state.pendingDpi
      state.pendingDpi = -1
      if (v >= 0) root.send(M.dpiArg(v))
    }
  }

  Process {
    id: reader
    command: Cli.command(root.periPath, ["status", root.moduleId])
    stdout: StdioCollector {
      onStreamFinished: state.s = M.parseStatus(this.text)
    }
    stderr: StdioCollector {
      onStreamFinished: { if (this.text.trim() !== "") state.error = this.text.trim() }
    }
    onExited: function(exitCode) {
      if (exitCode !== 0) state.s = M.parseStatus("")
    }
  }

  Process {
    id: writer
    stdout: StdioCollector {
      onStreamFinished: {
        var r = M.parseSet(this.text)
        if (!r.ok) return
        // Show what the device says now (the readback), not what was asked for.
        var next = Object.assign({}, state.s)
        if (r.dpi !== null) next.dpi = r.dpi
        if (r.poll > 0) next.poll = r.poll
        state.s = next
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { if (this.text.trim() !== "") state.error = this.text.trim() }
    }
  }
}
