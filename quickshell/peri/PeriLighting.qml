import QtQuick
import Quickshell
import Quickshell.Io

// The keyboard's lighting, as seen by the peri daemon. Works in any Quickshell
// config: it imports nothing from the host shell and draws nothing. Hosts
// bind to the properties below and decide how to look.
//
//   PeriLighting { id: peri }
//   MyIcon { color: peri.online && peri.lit ? peri.tint : "gray" }
//
// It reads through the `peri` CLI (`peri rgb get`), so it needs no knowledge
// of the daemon's socket or of the keyboard's protocol.
Scope {
  id: root

  // Where to find peri. If this path is not executable, `peri` on PATH is used.
  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property int interval: 30000 // ms between automatic refreshes; 0 disables

  readonly property bool online: state.online       // daemon and keyboard answered
  readonly property bool lit: state.lit             // lighting enabled on the keyboard
  readonly property string effect: state.effect     // e.g. "solid_color"
  readonly property int brightness: state.brightness // percent
  // The keyboard's own hue and saturation (0-255) as a color. Value is pinned
  // to 1 so it stays legible; the real brightness is `brightness`.
  readonly property color tint: state.tint

  readonly property bool busy: setter.running       // a change is in flight

  function refresh() {
    if (!query.running) query.running = true
  }

  // Change the running lighting, e.g. peri.set(["enable=0"]) or
  // peri.set(["hue=170", "sat=255"]). Takes the same key=value settings as
  // `peri rgb set`. The change is not saved to the keyboard's memory. Ignored
  // while another change is in flight; the keyboard's answer updates the
  // properties above.
  function set(settings) {
    if (setter.running || !settings || settings.length === 0) return
    setter.command = ["sh", "-c",
      "p=\"$1\"; [ -x \"$p\" ] || p=peri; shift; exec \"$p\" rgb set \"$@\"",
      "sh", root.periPath].concat(settings)
    setter.running = true
  }

  QtObject {
    id: state
    property bool online: false
    property bool lit: false
    property string effect: ""
    property int brightness: 0
    property color tint: "white"
  }

  // `peri rgb get` prints "key   value" lines, e.g. "hue      48".
  function parse(text) {
    var cfg = {}
    text.split("\n").forEach(function(line) {
      var m = line.match(/^(\S+)\s+(.+)$/)
      if (m) cfg[m[1]] = m[2]
    })
    if (cfg.hue === undefined) return // nothing usable; the Process decides about "online"

    state.online = true
    state.lit = cfg.enabled === "yes"
    state.effect = cfg.effect || ""
    state.brightness = Math.round(100 * Number(cfg.val) / 255)
    state.tint = Qt.hsva(Number(cfg.hue) / 255, Number(cfg.sat) / 255, 1, 1)
  }

  Process {
    id: query
    command: ["sh", "-c", "p=\"$1\"; [ -x \"$p\" ] || p=peri; exec \"$p\" rgb get",
              "sh", root.periPath]
    stdout: StdioCollector {
      onStreamFinished: root.parse(this.text)
    }
    onExited: function(exitCode) {
      if (exitCode !== 0) state.online = false
    }
  }

  // `peri rgb set` prints the resulting config in the same format as `get`,
  // so the reply refreshes our state directly. If it fails, ask again instead.
  Process {
    id: setter
    stdout: StdioCollector {
      onStreamFinished: root.parse(this.text)
    }
    onExited: function(exitCode) {
      if (exitCode !== 0) root.refresh()
    }
  }

  Timer {
    interval: root.interval
    running: root.interval > 0
    repeat: true
    triggeredOnStart: true
    onTriggered: root.refresh()
  }
}
