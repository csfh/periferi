.pragma library
.import "lighting.js" as L

// The DeathAdder pane's logic, free of UI and process code so it can be tested on its own. It reads
// `peri status <module>` and `peri set <module> dpi=.. | poll=..`.
//
// Every write may be kept in the mouse's own memory (the DPI set is always stored), so the pane
// only sends on a deliberate click, or on a slider's release after a pause: never while dragging.

// The DPI range the pane offers. The daemon enforces the module's own range on every write; this is
// the same range (the sensor's rated 30,000 DPI, 100 as the floor) so the slider never offers a
// value the daemon would refuse.
var minDpi = 100
var maxDpi = 30000
var dpiPresets = [400, 800, 1600, 3200]
var pollRates = [125, 500, 1000]

function clampDpi(v) {
  var n = Math.round(Number(v))
  if (isNaN(n)) return minDpi
  return Math.max(minDpi, Math.min(maxDpi, n))
}

// "800:800" -> { x: 800, y: 800 }, or null when it is not two whole numbers.
function parseDpi(text) {
  var m = /^(\d+):(\d+)$/.exec(String(text || ""))
  return m ? { x: Number(m[1]), y: Number(m[2]) } : null
}

// What `peri status` printed. Online means the mouse answered the firmware read (the daemon refuses
// with a message otherwise). Any single read the mouse answered with something other than ok prints
// "unknown": that field is -1 / null / 0 here, and the pane shows a dash.
//   battery: the raw 0-255 byte (-1 unknown); percent is derived and, like the scale, unproven.
//   poll: 0 when unknown or a code the daemon does not know.
function parseStatus(text) {
  var v = L.parseValues(text)
  var none = { online: false, battery: -1, batteryPercent: -1, charging: null, dpi: null, poll: 0 }
  if (!v.module || !v.firmware) return none
  var battery = /^\d+$/.test(v.battery || "") ? Number(v.battery) : -1
  if (battery > 255) battery = -1
  return {
    online: true,
    battery: battery,
    batteryPercent: battery < 0 ? -1 : Math.round(battery * 100 / 255),
    charging: v.charging === "yes" ? true : v.charging === "no" ? false : null,
    dpi: parseDpi(v.dpi),
    poll: /^\d+$/.test(v.poll || "") && pollRates.indexOf(Number(v.poll)) >= 0 ? Number(v.poll) : 0
  }
}

// `peri set` replies "sent  dpi:800", "before 1600:1600", "readback 800:800" (or "unavailable").
// The values read back are what the device says now; null when there was no readback or no reply.
function parseSet(text) {
  var v = L.parseValues(text)
  if (!v.module || !v.sent) return { ok: false, dpi: null, poll: 0 }
  var rb = v.readback || ""
  return { ok: true, dpi: parseDpi(rb), poll: /^\d+$/.test(rb) ? Number(rb) : 0 }
}

// What the pane draws: the status with what the user just chose laid over it (`shown`: "dpi" -> n, "poll" -> n)
// until the device has been read again, so a slider or a button never lags behind the click or snaps back.
function overlay(s, shown) {
  if (!s.online || !shown) return s
  var out = Object.assign({}, s)
  if (Object.prototype.hasOwnProperty.call(shown, "dpi")) out.dpi = { x: shown.dpi, y: shown.dpi }
  if (Object.prototype.hasOwnProperty.call(shown, "poll")) out.poll = shown.poll
  return out
}

// The status with a write's read-back (or, when the device gave none, the value that was sent) put in.
function patch(s, key, value, readback) {
  var out = Object.assign({}, s)
  if (key === "dpi") out.dpi = readback !== null && readback !== undefined ? readback : { x: value, y: value }
  if (key === "poll") out.poll = readback > 0 ? readback : value
  return out
}

function batteryText(s) {
  if (!s.online || s.battery < 0) return "Battery  -"
  var text = "Battery  " + s.batteryPercent + "%"
  if (s.charging === true) text += "  (charging)"
  return text
}

function dpiText(s) {
  return !s.online || s.dpi === null ? "DPI  -"
    : "DPI  " + (s.dpi.x === s.dpi.y ? s.dpi.x : s.dpi.x + " x " + s.dpi.y)
}

// Is this preset the mouse's current DPI? Only when both axes agree.
function dpiSelected(preset, s) {
  return s.online && s.dpi !== null && s.dpi.x === preset && s.dpi.y === preset
}

function pollSelected(rate, s) { return s.online && s.poll === rate }

// The argument of `peri set` for a DPI or a poll rate, or "" when the value is not one to send.
function dpiArg(v) { return "dpi=" + clampDpi(v) }
function pollArg(rate) { return pollRates.indexOf(rate) >= 0 ? "poll=" + rate : "" }
