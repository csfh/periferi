.pragma library
.import "lighting.js" as L

// The microphone pane's logic, free of UI and process code so it can be tested on its own.
// It reads `peri status` and `peri look ... get` and builds the arguments of `peri look ... save`.
//
// The pane edits two saved looks, Unmuted and Muted. A look is a set of the user's SETTINGS (an
// effect, a colour for the static effect, a brightness), kept by the daemon because the mic can
// neither report its effect or colour nor say which look is showing. Editing a look only saves it;
// nothing reaches the mic until the user presses Apply. That keeps every write to the mic a
// deliberate click, which matters because a write may be kept in the mic's own memory.

// The module declares the protocol's one-byte range as its brightness limit.
var maxBrightness = 255

// The two looks, in the order the switch shows them.
var looks = [
  { id: "unmuted", label: "Unmuted look" },
  { id: "muted", label: "Muted look" }
]

// The effect buttons. The highlighted one is the one saved in the look being edited.
var effects = [
  { id: "static", label: "Colour", needsColor: true },
  { id: "breathing", label: "Breathing", needsColor: false }, // pulses through random colours
  { id: "spectrum", label: "Spectrum", needsColor: false },    // cycles through every colour
  { id: "none", label: "Off", needsColor: false }
]

// Colour swatches for the "Colour" effect: fixed presets, not a drag-driven picker.
var presets = [
  { name: "Red", hex: "ff0000" }, { name: "Orange", hex: "ff6a00" },
  { name: "Yellow", hex: "ffd800" }, { name: "Green", hex: "00ff40" },
  { name: "Cyan", hex: "00e0ff" }, { name: "Blue", hex: "0000ff" },
  { name: "Purple", hex: "8000ff" }, { name: "White", hex: "ffffff" }
]

function validHex(hex) { return typeof hex === "string" && /^[0-9a-fA-F]{6}$/.test(hex) }

function knownEffect(id) {
  return effects.some(function(x) { return x.id === id })
}

function emptyLook() { return { effect: "", color: "", brightness: -1 } }

// `peri status` prints "key   value" lines. Anything that is not a full answer, such as an empty
// reply or a refusal, means the mic is offline. Only what the pane uses is read: brightness is the
// mic's own value, shown on the slider while the look being edited has none saved.
function parseStatus(text) {
  var v = L.parseValues(text)
  var brightness = /^\d+$/.test(v.brightness || "") ? Number(v.brightness) : -1
  if (!v.module || brightness < 0 || brightness > maxBrightness)
    return { online: false, brightness: 0 }
  return { online: true, brightness: brightness }
}

// `peri look <module> get|save <which>` prints the look as "key   value" lines, with "unset" for an
// effect or brightness that is not saved and "none" for no colour. Returns null if that is not a
// look (a refusal, or nothing), so a failed read is never mistaken for an empty look.
function parseLook(text) {
  var v = L.parseValues(text)
  if (!v.module || !v.look) return null
  var look = emptyLook()
  if (knownEffect(v.effect)) look.effect = v.effect
  if (look.effect === "static" && validHex(v.color)) look.color = v.color.toLowerCase()
  else if (look.effect === "static") look.effect = ""
  if (/^\d+$/.test(v.brightness || "") && Number(v.brightness) <= maxBrightness)
    look.brightness = Number(v.brightness)
  return look
}

function brightnessPercent(value) { return L.valToPercent(value, maxBrightness) }

// Pressing an effect button or a swatch: the look with that effect. "static" needs a colour (a
// swatch); every other effect drops it. null for an unknown effect or a missing or bad colour.
function withEffect(look, id, hex) {
  var e = effects.filter(function(x) { return x.id === id })[0]
  if (!e) return null
  if (e.needsColor && !validHex(hex)) return null
  return { effect: id, color: e.needsColor ? hex.toLowerCase() : "", brightness: look.brightness }
}

// A released slider: the look with that brightness, or null when it would change nothing (so
// nothing is saved for a release that did not move the value).
function withBrightness(look, percent) {
  var value = L.percentToVal(percent, maxBrightness)
  if (value === look.brightness) return null
  return { effect: look.effect, color: look.color, brightness: value }
}

// The arguments of `peri look <module> ...` that save a whole look. A field that is not set is
// left out, which the daemon reads as "not set": the look is replaced, never merged.
function saveArgs(which, look) {
  var args = ["save", which]
  if (look.effect !== "") args.push("effect=" + look.effect)
  if (look.effect === "static" && look.color !== "") args.push("color=" + look.color)
  if (look.brightness >= 0) args.push("brightness=" + look.brightness)
  return args
}

function applyArgs(which) { return ["apply", which] }

// Is there anything to send? An empty look has nothing to apply.
function canApply(look) { return !!look && (look.effect !== "" || look.brightness >= 0) }

function effectSelected(id, look) {
  return !!look && look.effect !== "" && look.effect === id
}

function swatchSelected(hex, look) {
  return !!look && look.effect === "static" && validHex(hex) && look.color === hex.toLowerCase()
}

function lookLabel(which) {
  var l = looks.filter(function(x) { return x.id === which })[0]
  return l ? l.label : ""
}

// The saved look in words, for the line under the buttons.
function lookText(look) {
  if (!canApply(look)) return "Nothing saved in this look yet"
  var parts = []
  if (look.effect !== "") {
    var e = effects.filter(function(x) { return x.id === look.effect })[0]
    parts.push(look.effect === "static" ? "colour " + look.color : e.label.toLowerCase())
  }
  if (look.brightness >= 0) parts.push("brightness " + brightnessPercent(look.brightness) + "%")
  return "Saved: " + parts.join(", ")
}

// What Apply sends, in words, for the line beside the button.
function applyText(which) {
  return "Sends the " + lookLabel(which).toLowerCase() + " to the microphone now, "
    + "whatever its mute state."
}

// --- audio: the microphone's standard USB audio controls (`peri audio`) -----------------------
// Levels are 0-100 over each control's own range. They are live settings of the sound card, not
// stored by peri, so unlike a look they are applied as soon as a slider is released.

function emptyAudio() { return { gain: -1, monitor: -1, monitorOn: null, headphones: -1 } }

function level(text) {
  return /^\d+$/.test(text || "") && Number(text) <= 100 ? Number(text) : -1
}

// `peri audio <module> get|set` prints "key   value" lines. null if that is not an audio answer
// (a refusal, or nothing); a field the card did not report is -1 (or null for the switch).
function parseAudio(text) {
  var v = L.parseValues(text)
  if (!v.module) return null
  return {
    gain: level(v.gain),
    monitor: level(v.monitor),
    monitorOn: v.monitor_on === "yes" ? true : v.monitor_on === "no" ? false : null,
    headphones: level(v.headphones)
  }
}

var audioKeys = ["gain", "monitor", "monitor_on", "headphones"]

// A released slider or a pressed button: the "key=value" setting for `peri audio set`, or null
// when it would change nothing (a release that did not move the value is not sent) or the key or
// value is not one the daemon takes. `current` is what parseAudio last returned.
function audioSetting(key, value, current) {
  if (audioKeys.indexOf(key) < 0) return null
  if (key === "monitor_on") {
    if (typeof value !== "boolean") return null
    return current && current.monitorOn === value ? null : "monitor_on=" + (value ? "yes" : "no")
  }
  var n = Math.round(Number(value))
  if (!isFinite(n)) n = 0
  n = Math.max(0, Math.min(100, n))
  var field = key === "gain" ? "gain" : key === "monitor" ? "monitor" : "headphones"
  return current && current[field] === n ? null : key + "=" + n
}

// The audio state after a gesture, for showing it at once (the daemon's answer confirms it). Same
// rounding and clamping as audioSetting(). A key that is not an audio key leaves it as it is.
function withAudio(current, key, value) {
  var a = { gain: -1, monitor: -1, monitorOn: null, headphones: -1 }
  if (current) a = { gain: current.gain, monitor: current.monitor, monitorOn: current.monitorOn, headphones: current.headphones }
  if (key === "monitor_on" && typeof value === "boolean") { a.monitorOn = value; return a }
  if (key !== "gain" && key !== "monitor" && key !== "headphones") return a
  var n = Math.round(Number(value))
  a[key] = Math.max(0, Math.min(100, isFinite(n) ? n : 0))
  return a
}
