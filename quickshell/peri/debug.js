.pragma library

// The pane's debug log: pure formatting, so it can be tested on its own. PeriMic writes the lines
// (see its `debug` property); this file decides what they look like and explains each peri command
// in words, so a log can be read without knowing the code.
//
// A line is "HH:MM:SS.mmm  TAG        text". The tags:
var legend = [
  ["OPEN", "the debug log was switched on, with a snapshot of what the pane held"],
  ["GESTURE", "something the user pressed or released in the pane"],
  ["IGNORED", "a gesture or action that was deliberately not acted on, and why"],
  ["SAVE", "a look being saved: a file only, the microphone is not touched"],
  ["APPLY", "the Apply button: the one thing here that writes the look to the microphone"],
  ["AUDIO", "a sound-card level being set (live, not stored by peri)"],
  ["REFRESH", "the pane reading the microphone and the saved looks"],
  ["RUN", "a peri command starting, and what kind it is"],
  ["EXIT", "that command finishing: exit code and how long it took"],
  ["OUT", "what the command printed (shortened)"],
  ["ERR", "what the command printed on its error output (shortened)"],
  ["STATE", "what the pane now holds after reading something"]
]

function two(n) { return (n < 10 ? "0" : "") + n }
function three(n) { return (n < 10 ? "00" : n < 100 ? "0" : "") + n }

function stamp(d) {
  return two(d.getHours()) + ":" + two(d.getMinutes()) + ":" + two(d.getSeconds()) + "." + three(d.getMilliseconds())
}

function pad(text, width) {
  var s = String(text)
  while (s.length < width) s += " "
  return s
}

function line(d, tag, text) { return stamp(d) + "  " + pad(tag, 8) + " " + text }

// A text on one line, cut to `max` characters: a command's output with its line breaks shown as " | ".
function oneLine(text, max) {
  var s = String(text === undefined || text === null ? "" : text).replace(/\s*\n\s*/g, " | ").replace(/^[ |]+|[ |]+$/g, "")
  return s.length > max ? s.substring(0, max) + "... (" + s.length + " characters)" : s
}

// The peri arguments of a command built by cli.js: ["sh", "-c", script, "sh", peri, ...args].
function periArgs(command) {
  var c = command || []
  return c.length > 5 && c[0] === "sh" ? c.slice(5) : c
}

// What a peri command does, in words: its tag, the kind of effect it has, and a sentence.
// args are the peri arguments, for example ["look", "razer-seiren-v3-chroma", "apply", "muted"].
function classify(args) {
  var a = args || []
  var verb = a[0], rest = a.slice(2)
  if (verb === "status")
    return { kind: "read", note: "READ: asks the daemon for the mic's firmware and brightness (two read frames to the mic)" }
  if (verb === "audio")
    return a[2] === "set"
      ? { kind: "card", note: "SOUND-CARD WRITE: sets live mixer levels on the sound card (" + rest.slice(1).join(" ") + "); not stored by peri" }
      : { kind: "read", note: "READ: reads the sound card's mixer levels" }
  if (verb === "set")
    return { kind: "mic", note: "MIC WRITE: sends a lighting frame to the microphone now (" + rest.join(" ") + "); the mic may keep it" }
  if (verb === "look") {
    var what = a[2]
    if (what === "get")
      return { kind: "file-read", note: "FILE READ: reads the saved " + a[3] + " look; the microphone is not touched" }
    if (what === "save")
      return { kind: "file", note: "FILE WRITE: saves the " + a[3] + " look (" + a.slice(4).join(" ") + "); the microphone is not touched" }
    if (what === "apply")
      return { kind: "mic", note: "MIC WRITE: sends the saved " + a[3] + " look to the microphone, its effect and then its brightness; the mic may keep it" }
  }
  return { kind: "other", note: "a peri command this log does not explain" }
}

// A look in one line: its effect, colours, speed and brightness, with "-" for anything not saved.
function describeLook(look) {
  if (!look) return "(none)"
  return "effect=" + (look.effect || "-") + " color=" + (look.color || "-") + " color2=" + (look.color2 || "-")
    + " speed=" + (look.speed || "-") + " brightness=" + (look.brightness >= 0 ? look.brightness : "-")
}

function describeAudio(a) {
  if (!a) return "(none)"
  function lv(v) { return v >= 0 ? v + "%" : "unknown" }
  return "gain=" + lv(a.gain) + " monitor=" + lv(a.monitor) + " monitor_on=" + (a.monitorOn === null ? "unknown" : a.monitorOn ? "yes" : "no")
    + " headphones=" + lv(a.headphones)
}

// The first lines of a log: what it is, how to read it, and a snapshot of the pane. `info` has
// when (a Date), path, module, periPath, editing, online, looks, audio.
function header(info) {
  var out = []
  out.push("peri pane debug log, started " + info.when.toString())
  out.push("log file: " + info.path)
  out.push("module: " + (info.module || "(none yet)") + "    peri: " + info.periPath)
  out.push("")
  out.push("Every line is \"time  TAG  text\". Tags:")
  legend.forEach(function(t) { out.push("  " + pad(t[0], 8) + " " + t[1]) })
  out.push("")
  out.push("What the pane held when the log started:")
  out.push("  editing: the " + info.editing + " look")
  out.push("  microphone online: " + (info.online ? "yes" : "no"))
  out.push("  unmuted look: " + describeLook(info.looks && info.looks.unmuted))
  out.push("  muted look:   " + describeLook(info.looks && info.looks.muted))
  out.push("  audio: " + describeAudio(info.audio))
  out.push("")
  return out.join("\n")
}
