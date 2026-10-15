.pragma library

// The lighting controls' logic, free of UI and process code so it can be tested on its own.

// Brightness is a percentage of the keyboard's own limit (`maxval`, from its module), never
// of 255. With no known limit nothing can be sent: the daemon would refuse it anyway.
function percentToVal(percent, max) {
  if (!(max > 0)) return 0
  var p = Number(percent)
  if (!isFinite(p)) return 0
  return Math.round(Math.max(0, Math.min(100, p)) * max / 100)
}

function valToPercent(val, max) {
  if (!(max > 0)) return 0
  return Math.max(0, Math.min(100, Math.round(100 * Number(val) / max)))
}

// LED groups: bits 1 modifiers, 2 underglow, 4 keys, 8 indicators (QMK convention, which the
// board's own LED layout agrees with). The board holds more bits than these four (0xff), and a
// toggle changes only its own bit, so the others survive.
function setFlag(flags, bit, on) {
  return on ? (flags | bit) : (flags & ~bit & 0xff)
}

function groupsLit(flags) {
  return (flags & 0x0f) !== 0
}

// The flags after switching one group, or null if that would leave every group off (flags with
// no group bit mean a dark keyboard; the on/off toggle is the way to do that).
function flagChange(flags, bit, on) {
  var next = setFlag(flags, bit, on)
  return groupsLit(next) ? next : null
}

// Saving writes the keyboard's memory, so it never starts while a change or another save is in
// flight (the saved state would not be the one the user was looking at).
function canSave(settingInFlight, saving) {
  return !settingInFlight && !saving
}

// While one `peri rgb set` runs, later requests merge into one pending set instead of being
// dropped: per key the last value wins, and different controls combine. A later key moves to
// the end, so `color=` followed by `hue=` is still sent in that order. Inputs are not modified.
function mergeSettings(pending, incoming) {
  var out = Array.isArray(pending) ? pending.filter(valid) : []
  ;(Array.isArray(incoming) ? incoming : []).filter(valid).forEach(function(s) {
    var key = keyOf(s)
    out = out.filter(function(old) { return keyOf(old) !== key })
    out.push(s)
  })
  return out
}

function keyOf(setting) { return setting.substring(0, setting.indexOf("=")) }

// What the pane should DRAW for a list of settings that are on their way to the keyboard (in flight or waiting):
// { brightness (percent), lit, effect, hue, sat, speed, flags }, only for the keys present. The pane draws these
// over what the keyboard last said until the keyboard has answered, so a slider or a button never lags behind the
// click or snaps back when the mouse is released. `maxVal` is the keyboard's own brightness limit.
function shownFrom(settings, maxVal) {
  var out = {}
  ;(Array.isArray(settings) ? settings : []).filter(valid).forEach(function(s) {
    var key = keyOf(s), v = s.substring(s.indexOf("=") + 1)
    if (key === "val") out.brightness = valToPercent(Number(v), maxVal)
    else if (key === "enable") out.lit = v === "1" || v === "yes"
    else if (key === "effect") out.effect = v
    else if (key === "hue" || key === "sat" || key === "speed") out[key] = Number(v)
    else if (key === "flags") out.flags = parseInt(v)
  })
  return out
}
function valid(setting) { return typeof setting === "string" && setting.indexOf("=") > 0 }

// `peri rgb get` and `set` print "key   value" lines, e.g. "hue      48".
function parseValues(text) {
  var values = {}
  String(text).split("\n").forEach(function(line) {
    var m = line.match(/^(\S+)\s+(.+)$/)
    if (m) values[m[1]] = m[2]
  })
  return values
}

// `peri rgb effects` prints "effects  solid_color,band_pinwheel_sat,...": the firmware's own list.
function parseEffects(text) {
  var m = String(text).match(/^effects\s+(\S+)/m)
  if (!m) return []
  return m[1].split(",").filter(function(n) { return n.length > 0 }).map(function(name) {
    var words = name.replace(/_/g, " ")
    return { name: name, label: words.charAt(0).toUpperCase() + words.slice(1) }
  })
}

// A hue/saturation picker is a plane: hue runs left to right (0-255, the keyboard's own scale),
// saturation from 0 at the bottom to 255 at the top. Brightness is not on it; it has its own
// slider because it is capped. Points outside the plane snap to its edge, so a drag that leaves
// the picker keeps working.
function planeToHueSat(x, y, width, height) {
  if (!(width > 0) || !(height > 0)) return { hue: 0, sat: 0 }
  var fx = Math.max(0, Math.min(1, (Number(x) || 0) / width))
  var fy = Math.max(0, Math.min(1, (Number(y) || 0) / height))
  return { hue: Math.round(fx * 255), sat: Math.round((1 - fy) * 255) }
}

// Where the marker sits for a hue and saturation: the inverse of planeToHueSat.
function hueSatToPlane(hue, sat, width, height) {
  var h = Math.max(0, Math.min(255, Number(hue) || 0))
  var s = Math.max(0, Math.min(255, Number(sat) || 0))
  return { x: h / 255 * width, y: (1 - s / 255) * height }
}
