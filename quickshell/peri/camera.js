.pragma library
.import "lighting.js" as L

// The camera pane's logic, free of UI and process code so it can be tested on its own. It reads
// `peri controls <module>` (every control with its type, range, step, value and flags, and each menu's
// options) and builds the argument of `peri set <module> <control>=<value>`.
//
// The pane is built from what the camera lists, not from a fixed set of controls: a control it does not
// know still gets a title and a widget. Only controls the daemon says may be set (flag "s") are shown;
// the others are the camera's own business. Every write may be kept by the camera, so the pane only sends
// on a deliberate click, or on a slider's release after a pause: never while dragging.

// Friendly titles for the controls a UVC camera usually has. Anything else is titled from its name.
var titles = {
  brightness: "Brightness",
  contrast: "Contrast",
  saturation: "Saturation",
  sharpness: "Sharpness",
  gain: "Gain",
  backlight_compensation: "Backlight compensation",
  white_balance_automatic: "Auto white balance",
  white_balance_temperature: "White balance",
  auto_exposure: "Exposure mode",
  exposure_time_absolute: "Exposure time",
  exposure_dynamic_framerate: "Low-light compensation",
  power_line_frequency: "Anti-flicker",
  focus_automatic_continuous: "Autofocus",
  focus_absolute: "Focus",
  zoom_absolute: "Zoom",
  field_of_view: "Field of view",
  pan_absolute: "Pan",
  tilt_absolute: "Tilt"
}

// The sections of the pane, in order. A control not named here goes under "Other".
var sections = [
  { title: "Image", names: ["brightness", "contrast", "saturation", "sharpness", "gain", "backlight_compensation"] },
  { title: "White balance", names: ["white_balance_automatic", "white_balance_temperature"] },
  { title: "Exposure", names: ["auto_exposure", "exposure_time_absolute", "exposure_dynamic_framerate", "power_line_frequency"] },
  { title: "Focus and framing", names: ["field_of_view", "focus_automatic_continuous", "focus_absolute", "zoom_absolute", "pan_absolute", "tilt_absolute"] }
]

function titleOf(name) {
  if (titles[name]) return titles[name]
  var words = String(name).replace(/_/g, " ").trim()
  return words.charAt(0).toUpperCase() + words.slice(1)
}

// "1:Manual_Mode,3:Aperture_Priority_Mode" -> [{ index: 1, label: "Manual Mode" }, ...]
function parseOptions(text) {
  var out = []
  String(text || "").split(",").forEach(function(part) {
    var m = /^(\d+):(.+)$/.exec(part)
    if (m) out.push({ index: Number(m[1]), label: m[2].replace(/_/g, " ") })
  })
  return out
}

// "int:0:255:1:128:s" -> a control, or null when it is not one. flags: s = may be set, i = inactive now,
// r = read-only, "-" = none.
function parseControl(name, text) {
  var m = /^(int|bool|menu):(-?\d+):(-?\d+):(-?\d+):(-?\d+):([sir-]+)$/.exec(String(text))
  if (!m) return null
  var min = Number(m[2]), max = Number(m[3]), step = Math.max(1, Number(m[4]))
  // An integer that only takes 0 and 1 is a switch whatever the kernel calls it (the BRIO's backlight
  // compensation is one), so the pane draws it as a toggle.
  var type = m[1] === "int" && min === 0 && max === 1 && step === 1 ? "bool" : m[1]
  return {
    name: name, title: titleOf(name), type: type,
    min: min, max: max, step: step, value: Number(m[5]),
    settable: m[6].indexOf("s") >= 0, inactive: m[6].indexOf("i") >= 0, readOnly: m[6].indexOf("r") >= 0,
    options: []
  }
}

// What `peri controls` printed. Online means the daemon answered with at least one control (a refusal,
// or nothing, means offline, so a pane never shows stale numbers as live). Only controls that may be set
// are kept, in the order the camera listed them.
function parseControls(text) {
  var v = L.parseValues(text)
  var none = { online: false, controls: [] }
  if (!v.module) return none
  var list = []
  Object.keys(v).forEach(function(key) {
    if (key === "module" || /\.options$/.test(key)) return
    var c = parseControl(key, v[key])
    if (c === null || !c.settable) return
    if (c.type === "menu") c.options = parseOptions(v[key + ".options"])
    list.push(c)
  })
  return list.length === 0 ? none : { online: true, controls: list }
}

// The rows of the pane: a header row for each section that has controls, then its controls. Controls the
// sections do not name come last, under "Other". Each row is { kind: "header", title } or
// { kind: "control", control }.
function rows(controls) {
  var byName = {}
  controls.forEach(function(c) { byName[c.name] = c })
  var used = {}
  var out = []
  sections.forEach(function(s) {
    var here = s.names.filter(function(n) { return byName[n] })
    if (here.length === 0) return
    out.push({ kind: "header", title: s.title })
    here.forEach(function(n) { used[n] = true; out.push({ kind: "control", control: byName[n] }) })
  })
  var rest = controls.filter(function(c) { return !used[c.name] })
  if (rest.length > 0) {
    out.push({ kind: "header", title: "Other" })
    rest.forEach(function(c) { out.push({ kind: "control", control: c }) })
  }
  return out
}

// The rows split into two columns of about the same height, always at a section boundary (a header never
// ends a column). Weight counts a slider or a menu as two rows and a switch as one, as they are drawn.
function columns(rowList) {
  function weight(r) {
    if (r.kind === "header") return 1
    return r.control.type === "bool" ? 1 : 2
  }
  var total = rowList.reduce(function(sum, r) { return sum + weight(r) }, 0)
  var best = rowList.length, bestGap = Infinity, acc = 0
  for (var i = 0; i < rowList.length; i++) {
    if (i > 0 && rowList[i].kind === "header") {
      var gap = Math.abs(total - 2 * acc)
      if (gap < bestGap) { bestGap = gap; best = i }
    }
    acc += weight(rowList[i])
  }
  return [rowList.slice(0, best), rowList.slice(best)]
}

function byName(controls, name) {
  for (var i = 0; i < controls.length; i++) if (controls[i].name === name) return controls[i]
  return null
}

// Is this value one the control accepts right now? The daemon checks everything again and the camera
// has the last word; this keeps the pane from offering what would be refused.
function valid(c, v) {
  if (!c || !c.settable || c.readOnly || c.inactive) return false
  if (typeof v !== "number" || !isFinite(v) || Math.floor(v) !== v) return false
  if (v < c.min || v > c.max) return false
  if (c.type === "bool") return v === 0 || v === 1
  if (c.type === "menu") return c.options.some(function(o) { return o.index === v })
  return (v - c.min) % c.step === 0
}

// The nearest value the control takes: clamped to its range and rounded to its step.
function snap(c, v) {
  var n = Math.round(Number(v))
  if (isNaN(n)) return c.min
  n = Math.max(c.min, Math.min(c.max, n))
  if (c.type === "int" && c.step > 1) n = c.min + Math.round((n - c.min) / c.step) * c.step
  return Math.max(c.min, Math.min(c.max, n))
}

// The argument of `peri set <module>` for this control and value, or "" when it is not one to send.
function setArg(c, v) {
  return valid(c, v) ? c.name + "=" + v : ""
}

function isCurrent(c, v) { return !!c && c.value === v }

// A control's value as text for its label: "128", "On", "60 Hz".
function valueText(c) {
  if (c.type === "bool") return c.value ? "On" : "Off"
  if (c.type === "menu") {
    var o = c.options.filter(function(x) { return x.index === c.value })
    return o.length ? o[0].label : String(c.value)
  }
  return String(c.value)
}

// What `peri set` replied: { ok, name, value } where value is the camera's own read-back (which only
// echoes the driver's cache), or ok false for a refusal or nothing.
function parseSet(text) {
  var v = L.parseValues(text)
  if (!v.module || !v.sent) return { ok: false, name: "", value: null }
  // A reply that sent nothing ("sent none": the value was already set) is not a write.
  var sent = /^([a-z0-9_]+):(-?\d+)$/.exec(v.sent)
  if (!sent) return { ok: false, name: "", value: null }
  var rb = /^-?\d+$/.test(v.readback || "") ? Number(v.readback) : null
  return { ok: true, name: sent[1], value: rb }
}
