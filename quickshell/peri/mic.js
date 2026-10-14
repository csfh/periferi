.pragma library
.import "lighting.js" as L

// The microphone pane's logic, free of UI and process code so it can be tested on its own.
// It reads `peri status`, `peri look ... get` and `peri audio ... get`, and builds the arguments of
// `peri look ... save` and `peri audio ... set`.
//
// The pane edits two saved looks, Unmuted and Muted. A look is a set of the user's SETTINGS (an
// effect, up to two colours, a brightness), kept by the daemon because the mic can neither report
// its effect or colour nor say which look is showing. Editing a look only saves it; nothing reaches
// the mic until the user presses Apply. That keeps every write to the mic a deliberate click, which
// matters because a write may be kept in the mic's own memory.

// The module declares the protocol's one-byte range as its brightness limit.
var maxBrightness = 255

// The two looks, in the order the switch shows them.
var looks = [
  { id: "unmuted", label: "Unmuted look" },
  { id: "muted", label: "Muted look" }
]

// The effect buttons. `colors` is how many colours the effect takes (0, 1 or 2): "static" needs
// its one, "breathing" and "starlight" may have none (random colours), one or two. The highlighted
// button is the one saved in the look being edited.
var effects = [
  { id: "static", label: "Colour", colors: 1 },
  { id: "breathing", label: "Breathing", colors: 2 },
  { id: "spectrum", label: "Spectrum", colors: 0 },
  { id: "wave", label: "Wave", colors: 0 },
  { id: "wave-reverse", label: "Wave, other way", colors: 0 },
  { id: "wheel", label: "Wheel", colors: 0 },
  { id: "wheel-reverse", label: "Wheel, other way", colors: 0 },
  { id: "starlight", label: "Starlight", colors: 2 },
  { id: "none", label: "Off", colors: 0 }
]

// Colour swatches: fixed presets, not a drag-driven picker.
var presets = [
  { name: "Red", hex: "ff0000" }, { name: "Orange", hex: "ff6a00" },
  { name: "Yellow", hex: "ffd800" }, { name: "Green", hex: "00ff40" },
  { name: "Cyan", hex: "00e0ff" }, { name: "Blue", hex: "0000ff" },
  { name: "Purple", hex: "8000ff" }, { name: "White", hex: "ffffff" }
]

function validHex(hex) { return typeof hex === "string" && /^[0-9a-fA-F]{6}$/.test(hex) }

function effectInfo(id) {
  return effects.filter(function(x) { return x.id === id })[0] || null
}
function knownEffect(id) { return effectInfo(id) !== null }

// How many colours an effect takes: 0 for an unknown or colourless one.
function maxColors(id) {
  var e = effectInfo(id)
  return e ? e.colors : 0
}

function emptyLook() { return { effect: "", color: "", color2: "", brightness: -1 } }

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
// look (a refusal, or nothing), so a failed read is never mistaken for an empty look. A field the
// effect cannot have (a colour on a wave, a second colour without a first) is dropped.
function parseLook(text) {
  var v = L.parseValues(text)
  if (!v.module || !v.look) return null
  var look = emptyLook()
  if (knownEffect(v.effect)) look.effect = v.effect
  var n = maxColors(look.effect)
  if (n >= 1 && validHex(v.color)) look.color = v.color.toLowerCase()
  if (look.effect === "static" && look.color === "") look.effect = ""   // static needs its colour
  if (n === 2 && look.color !== "" && validHex(v.color2)) look.color2 = v.color2.toLowerCase()
  if (/^\d+$/.test(v.brightness || "") && Number(v.brightness) <= maxBrightness)
    look.brightness = Number(v.brightness)
  return look
}

function brightnessPercent(value) { return L.valToPercent(value, maxBrightness) }

function copyLook(look, changes) {
  var out = { effect: look.effect, color: look.color, color2: look.color2, brightness: look.brightness }
  for (var k in changes) out[k] = changes[k]
  return out
}

// Pressing an effect button: the look with that effect. The colours the new effect can use are
// kept and the rest dropped, and "Colour" with no colour yet starts on white so it can be saved.
// null for an unknown effect.
function withEffect(look, id) {
  var n = maxColors(id)
  if (!knownEffect(id)) return null
  var color = n >= 1 ? look.color : ""
  var color2 = n === 2 && color !== "" ? look.color2 : ""
  if (id === "static" && color === "") color = "ffffff"
  return copyLook(look, { effect: id, color: color, color2: color2 })
}

// Pressing a swatch in the first row: colours the current effect if it takes a colour, else
// switches to "Colour" (static) in that colour. In the second row (slot 2): the second colour,
// only for an effect that takes two and already has a first. null for a bad colour or slot.
function withColor(look, slot, hex) {
  if (!validHex(hex)) return null
  hex = hex.toLowerCase()
  if (slot === 2) {
    if (maxColors(look.effect) !== 2 || look.color === "") return null
    return copyLook(look, { color2: hex })
  }
  if (slot !== 1) return null
  if (maxColors(look.effect) >= 1)
    return copyLook(look, { color: hex, color2: maxColors(look.effect) === 2 ? look.color2 : "" })
  return copyLook(look, { effect: "static", color: hex, color2: "" })
}

// "Random colours": breathing or starlight with no colour chosen. null for any other effect.
function withRandomColors(look) {
  if (maxColors(look.effect) !== 2) return null
  return copyLook(look, { color: "", color2: "" })
}

// A released slider: the look with that brightness, or null when it would change nothing (so
// nothing is saved for a release that did not move the value).
function withBrightness(look, percent) {
  var value = L.percentToVal(percent, maxBrightness)
  if (value === look.brightness) return null
  return copyLook(look, { brightness: value })
}

// The arguments of `peri look <module> ...` that save a whole look. A field that is not set is
// left out, which the daemon reads as "not set": the look is replaced, never merged.
function saveArgs(which, look) {
  var args = ["save", which]
  var n = maxColors(look.effect)
  if (look.effect !== "") args.push("effect=" + look.effect)
  if (n >= 1 && look.color !== "") args.push("color=" + look.color)
  if (n === 2 && look.color !== "" && look.color2 !== "") args.push("color2=" + look.color2)
  if (look.brightness >= 0) args.push("brightness=" + look.brightness)
  return args
}

function applyArgs(which) { return ["apply", which] }

// Is there anything to send? An empty look has nothing to apply.
function canApply(look) { return !!look && (look.effect !== "" || look.brightness >= 0) }

// Is the second row of swatches useful? Only for an effect that takes two colours and has a first.
function showSecondColor(look) { return !!look && maxColors(look.effect) === 2 && look.color !== "" }

// Does the "Random colours" button apply? Breathing and starlight, with or without colours.
function showRandomColors(look) { return !!look && maxColors(look.effect) === 2 }

function effectSelected(id, look) {
  return !!look && look.effect !== "" && look.effect === id
}

// Is this swatch the saved colour in that slot (1 or 2)?
function swatchSelected(hex, look, slot) {
  if (!look || !validHex(hex)) return false
  return (slot === 2 ? look.color2 : look.color) === hex.toLowerCase()
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
    var name = effectInfo(look.effect).label.toLowerCase()
    if (look.effect === "static") name = "colour " + look.color
    else if (look.color !== "") name += ", " + (look.color2 !== "" ? "colours " + look.color + " and " + look.color2
                                                                   : "colour " + look.color)
    else if (maxColors(look.effect) === 2) name += ", random colours"
    parts.push(name)
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
