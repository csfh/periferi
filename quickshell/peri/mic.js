.pragma library
.import "lighting.js" as L

// The microphone pane's logic, free of UI and process code so it can be tested on its own.
// It reads `peri status <module>` and builds the arguments of `peri set <module> ...`.
//
// Two facts shape it. The mic cannot be asked for its effect or colour, so the daemon keeps a
// record of what peri last SENT it (`effect` and `color` in `peri status`), and that record is
// all the pane can show: it is not what the mic is displaying, and a change made any other way
// is not in it. And a set may be kept in the device's own memory, so a value is only ever sent
// from a deliberate gesture (a slider released, a button pressed), and never because a refresh
// changed what a slider shows.

// The module declares the protocol's one-byte range as its brightness limit.
var maxBrightness = 255

// The effect buttons. The highlighted one is the last the mic accepted from peri, not a reading
// of what the mic shows.
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

// `peri status` prints "key   value" lines (module, firmware, brightness, mute, dial, effect,
// color). Anything that is not a full answer, such as an empty reply or a refusal, means the mic
// is offline. effect is "" when peri has no record (the daemon prints "unknown"), and color is ""
// unless the recorded effect is "static". firmware is not shown: what its bytes mean is not
// established.
function parseStatus(text) {
  var v = L.parseValues(text)
  var brightness = /^\d+$/.test(v.brightness || "") ? Number(v.brightness) : -1
  if (!v.module || brightness < 0 || brightness > maxBrightness)
    return { online: false, brightness: 0, mute: "unknown", dial: null, firmware: "", effect: "", color: "" }
  return {
    online: true,
    brightness: brightness,
    mute: v.mute === "muted" || v.mute === "unmuted" ? v.mute : "unknown",
    dial: /^\d+$/.test(v.dial || "") ? Number(v.dial) : null,
    firmware: v.firmware || "",
    effect: knownEffect(v.effect) ? v.effect : "",
    color: v.effect === "static" && validHex(v.color) ? v.color.toLowerCase() : ""
  }
}

function knownEffect(id) {
  return effects.some(function(x) { return x.id === id })
}

function brightnessPercent(value) { return L.valToPercent(value, maxBrightness) }

// A released slider: the setting to send, or null when it would not change anything (a set
// that changes nothing still costs a write, so it is never sent).
function brightnessSetting(percent, currentValue) {
  var value = L.percentToVal(percent, maxBrightness)
  return value === currentValue ? null : "brightness=" + value
}

function validHex(hex) { return typeof hex === "string" && /^[0-9a-fA-F]{6}$/.test(hex) }

// The arguments of one effect button: ["effect=static", "color=0000ff"], ["effect=spectrum"], ...
// null for an unknown effect, or for "static" without a valid colour.
function effectSettings(id, hex) {
  var e = effects.filter(function(x) { return x.id === id })[0]
  if (!e) return null
  if (!e.needsColor) return ["effect=" + id]
  if (!validHex(hex)) return null
  return ["effect=" + id, "color=" + hex.toLowerCase()]
}

function muteText(mute) {
  if (mute === "muted") return "Muted"
  if (mute === "unmuted") return "Not muted"
  return "Mute state unknown until you tap the mic"
}

// The dial's level as it is reported (observed 20 to 100). It is not called gain or volume,
// because what it controls is not established.
function dialText(dial) {
  return dial === null ? "Dial not turned yet" : "Dial " + dial
}

// Is this the effect peri last sent? last = { effect, color } from parseStatus. The "Colour" effect
// has no button of its own: its swatches carry it.
function effectSelected(id, last) {
  return !!last && last.effect !== "" && last.effect === id
}

// Is this swatch the colour peri last sent? Only when the last effect was "static".
function swatchSelected(hex, last) {
  return !!last && last.effect === "static" && validHex(hex) && last.color === hex.toLowerCase()
}

function lastSetText(last) {
  if (!last || last.effect === "") return "Not set from peri yet"
  var e = effects.filter(function(x) { return x.id === last.effect })[0]
  var name = e.id === "static" ? "colour " + last.color : e.label.toLowerCase()
  return "Last set from peri: " + name
}
