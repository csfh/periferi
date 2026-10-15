import QtQuick
import Quickshell
import Quickshell.Io
import "lighting.js" as L
import "cli.js" as Cli
import "choices.js" as Ch

// The keyboard's lighting, as seen by the peri daemon. Works in any Quickshell
// config: it imports nothing from the host shell and draws nothing. Hosts
// bind to the properties below and decide how to look.
//
//   PeriLighting { id: peri }
//   MyIcon { color: peri.online && peri.lit ? peri.tint : "gray" }
//   MySlider { value: peri.brightness; onMoved: v => peri.setBrightness(v) }
//
// It reads and writes through the `peri` CLI (`peri rgb ...`), so it needs no
// knowledge of the daemon's socket or of the keyboard's protocol. Every change
// is to the keyboard's running state only; nothing is kept in its memory until
// save() is called, and nothing else calls it.
Scope {
  id: root

  // Where to find peri. If this path is not executable, `peri` on PATH is used.
  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property int interval: 30000 // ms between automatic refreshes; 0 disables

  // Each of these shows what the user just chose until the keyboard has answered (`state.shown`), else what the
  // keyboard last said: PanelSlider snaps its knob back to the model's value the moment the mouse is released,
  // so a value that lagged behind the click would jump back and then forward again.
  readonly property bool online: state.online       // daemon and keyboard answered
  readonly property bool lit: Ch.shownValue("lit", state.lit, state.shown)               // lighting enabled
  readonly property string effect: Ch.shownValue("effect", state.effect, state.shown)    // e.g. "solid_color"
  readonly property int hue: Ch.shownValue("hue", state.hue, state.shown)                // 0-255
  readonly property int sat: Ch.shownValue("sat", state.sat, state.shown)                // 0-255
  readonly property int speed: Ch.shownValue("speed", state.speed, state.shown)          // 0-255
  readonly property int flags: Ch.shownValue("flags", state.flags, state.shown)          // LED groups: 1 modifiers, 2 underglow, 4 keys, 8 indicators

  // Brightness as a percentage of the KEYBOARD'S OWN limit (its module's limits.max_brightness,
  // 191 on the CTRL v2), not of 255. 0 and canBrightness false if the module sets no limit; the
  // daemon would refuse any brightness then.
  readonly property int brightness: Ch.shownValue("brightness", state.brightness, state.shown)
  readonly property bool canBrightness: state.maxVal > 0

  // What the user chose and the keyboard has not answered yet (tests and debugging).
  readonly property var shown: state.shown

  // The firmware's own list of effects: [{ name: "solid_color", label: "Solid color" }, ...]
  readonly property var effects: state.effects

  // The keyboard's own hue and saturation as a color. Value is pinned to 1 so it stays
  // legible; the real brightness is `brightness`.
  readonly property color tint: Qt.hsva(root.hue / 255, root.sat / 255, 1, 1)

  readonly property bool busy: state.setting || saver.running // a change is in flight
  readonly property string error: state.error                 // why the last change failed, or ""
  readonly property bool saving: saver.running
  readonly property bool saved: state.saved                   // the last save worked; cleared by any change

  // Read the keyboard. Never while a change is in flight or waiting (the read would draw the old values over it):
  // the change's own answer carries the whole configuration anyway.
  function refresh() {
    if (query.running || state.setting || state.pending.length > 0) return
    state.readGen = state.writeGen
    query.running = true
  }

  // Change the running lighting, e.g. peri.set(["enable=0"]) or peri.set(["hue=170", "sat=255"]).
  // Takes the same key=value settings as `peri rgb set`. Not saved to the keyboard's memory.
  // While a change is in flight, further ones are merged (last value per key wins) and sent
  // when it finishes, so a dragged slider ends on the value it was released at. The keyboard's
  // answer updates the properties above.
  function set(settings) {
    var merged = L.mergeSettings([], settings)
    if (merged.length === 0) return
    // Draw the choice at once, over what the keyboard last said, until the keyboard has answered.
    state.shown = Object.assign({}, state.shown, L.shownFrom(merged, state.maxVal))
    if (state.setting) { state.pending = L.mergeSettings(state.pending, merged); return }
    send(merged)
  }

  function setBrightness(percent) {
    if (!canBrightness) return
    set(["val=" + L.percentToVal(percent, state.maxVal)])
  }

  function setEffect(name) { set(["effect=" + name]) }

  // Switch one LED group, changing only its bit. Switching off the last lit group is ignored:
  // that is a dark keyboard, and the on/off toggle is the way to do it. It builds on the flags the pane
  // SHOWS (a click that is still on its way counts), so two quick clicks on different groups both stick.
  function setGroup(bit, on) {
    var next = L.flagChange(root.flags, bit, on)
    if (next !== null) set(["flags=" + next])
  }

  // Write the running lighting to the keyboard's memory (EEPROM, which wears). Only ever
  // called by an explicit, confirmed user action; no other code path here reaches it.
  // Returns whether it started: false while a change is still running, so the caller can say so
  // instead of dropping the user's confirmation.
  function save() {
    if (!L.canSave(state.setting, saver.running)) return false
    state.error = ""
    state.saved = false
    saver.command = Cli.command(root.periPath, ["rgb", "save"])
    saver.running = true
    return true
  }

  QtObject {
    id: state
    property bool online: false
    property bool lit: false
    property string effect: ""
    property int hue: 0
    property int sat: 0
    property int speed: 0
    property int flags: 0
    property int maxVal: 0
    property int brightness: 0
    property var effects: []
    property bool setting: false
    property bool saved: false
    property var pending: []
    property var shown: ({})   // what the user chose and the keyboard has not answered yet (lighting.js shownFrom)
    property int writeGen: 0   // counts the changes sent; a read that began before the latest is stale
    property int readGen: 0
    property string error: ""
    property string errText: ""
  }

  function send(settings) {
    state.writeGen++
    state.setting = true
    state.saved = false
    state.errText = ""
    setter.command = Cli.command(root.periPath, ["rgb", "set"].concat(settings))
    setter.running = true
  }

  function parse(text) {
    var cfg = L.parseValues(text)
    if (cfg.hue === undefined) return // nothing usable; the Process decides about "online"

    state.online = true
    state.lit = cfg.enabled === "yes"
    state.effect = cfg.effect || ""
    state.hue = Number(cfg.hue)
    state.sat = Number(cfg.sat)
    state.speed = Number(cfg.speed)
    state.flags = parseInt(cfg.flags)
    state.maxVal = Number(cfg.maxval) || 0
    state.brightness = L.valToPercent(Number(cfg.val), state.maxVal)
  }

  Process {
    id: query
    command: Cli.command(root.periPath, ["rgb", "get"])
    stdout: StdioCollector {
      onStreamFinished: {
        // A change was sent after this read began: its answer is older than the change, so it is dropped (the
        // change's own reply carries the whole configuration).
        if (state.readGen !== state.writeGen || state.setting || state.pending.length > 0) return
        root.parse(this.text)
        state.shown = {} // the keyboard has been read again with nothing in flight: it is the truth
        if (state.online && state.effects.length === 0) effectsQuery.running = true
      }
    }
    onExited: function(exitCode) {
      if (exitCode !== 0) state.online = false
    }
  }

  // The firmware's list of enabled effects, read once it is online.
  Process {
    id: effectsQuery
    command: Cli.command(root.periPath, ["rgb", "effects"])
    stdout: StdioCollector {
      onStreamFinished: state.effects = L.parseEffects(this.text)
    }
  }

  // `peri rgb set` prints the resulting config in the same format as `get`, so the reply
  // refreshes our state directly. If it fails, show why and ask again instead.
  Process {
    id: setter
    stdout: StdioCollector {
      onStreamFinished: {
        root.parse(this.text)
        // The keyboard answered with its whole configuration: only what is still waiting stays drawn over it.
        state.shown = L.shownFrom(state.pending, state.maxVal)
      }
    }
    stderr: StdioCollector {
      onStreamFinished: state.errText = this.text.trim()
    }
    onExited: function(exitCode) {
      state.setting = false
      if (exitCode !== 0) state.shown = L.shownFrom(state.pending, state.maxVal) // a refused change: draw what is true
      state.error = exitCode === 0 ? "" : (state.errText || "the keyboard did not accept the change")
      if (exitCode !== 0) root.refresh()
      if (state.pending.length > 0) {
        var next = state.pending
        state.pending = []
        root.send(next)
      }
    }
  }

  Process {
    id: saver
    stderr: StdioCollector {
      onStreamFinished: state.errText = this.text.trim()
    }
    onExited: function(exitCode) {
      state.error = exitCode === 0 ? "" : (state.errText || "the keyboard did not save")
      state.saved = exitCode === 0
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
