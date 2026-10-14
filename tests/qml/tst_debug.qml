import QtQuick
import QtTest
import "../../quickshell/peri/debug.js" as D

TestCase {
  name: "debug"

  readonly property string id: "razer-seiren-v3-chroma"

  function test_line_format() {
    compare(D.line(new Date(2026, 9, 14, 3, 4, 5, 6), "GESTURE", "pressed Breathing"),
            "03:04:05.006  GESTURE  pressed Breathing")
    compare(D.line(new Date(2026, 9, 14, 15, 30, 1, 123), "RUN", "x"), "15:30:01.123  RUN      x")
    compare(D.line(new Date(2026, 9, 14, 0, 0, 0, 99), "APPLY", "y"), "00:00:00.099  APPLY    y")
  }

  function test_one_line() {
    compare(D.oneLine("module   m\nbrightness 255\n", 100), "module   m | brightness 255")
    compare(D.oneLine("", 10), "")
    compare(D.oneLine(undefined, 10), "")
    compare(D.oneLine(null, 10), "")
    compare(D.oneLine("abcdefghij", 5), "abcde... (10 characters)")
    compare(D.oneLine("\n\n  a  \n\n", 10), "a")
  }

  // The plain peri arguments come out of the wrapper cli.js builds.
  function test_peri_args() {
    compare(D.periArgs(["sh", "-c", "script", "sh", "/home/x/.local/bin/peri", "status", id]), ["status", id])
    compare(D.periArgs(["status", id]), ["status", id])
    compare(D.periArgs(undefined), [])
    compare(D.periArgs(["sh"]), ["sh"])
  }

  // Every kind of command says plainly whether it touches the microphone.
  function test_classify_reads() {
    compare(D.classify(["status", id]).kind, "read")
    compare(D.classify(["audio", id, "get"]).kind, "read")
    compare(D.classify(["look", id, "get", "muted"]).kind, "file-read")
    compare(D.classify(["look", id, "follow", "get"]).kind, "file-read")
    verify(D.classify(["look", id, "get", "muted"]).note.indexOf("microphone is not touched") >= 0)
  }

  function test_classify_writes() {
    compare(D.classify(["look", id, "apply", "unmuted"]).kind, "mic")
    verify(D.classify(["look", id, "apply", "unmuted"]).note.indexOf("MIC WRITE") === 0)
    verify(D.classify(["look", id, "apply", "unmuted"]).note.indexOf("unmuted look") >= 0)
    compare(D.classify(["set", id, "effect=wave"]).kind, "mic")
    compare(D.classify(["look", id, "save", "muted", "effect=wave", "speed=fast"]).kind, "file")
    verify(D.classify(["look", id, "save", "muted", "effect=wave", "speed=fast"]).note.indexOf("effect=wave speed=fast") >= 0)
    verify(D.classify(["look", id, "save", "muted"]).note.indexOf("microphone is not touched") >= 0)
    compare(D.classify(["look", id, "follow", "on"]).kind, "file")
    compare(D.classify(["audio", id, "set", "gain=40", "monitor_on=yes"]).kind, "card")
    verify(D.classify(["audio", id, "set", "gain=40"]).note.indexOf("gain=40") >= 0)
    compare(D.classify(["rgb", "get"]).kind, "other")
    compare(D.classify([]).kind, "other")
    compare(D.classify(undefined).kind, "other")
  }

  // Only the two commands that write to the microphone are classed as mic writes.
  function test_only_two_verbs_write_to_the_mic() {
    var all = [["status", id], ["audio", id, "get"], ["audio", id, "set", "gain=1"],
               ["look", id, "get", "unmuted"], ["look", id, "save", "unmuted"], ["look", id, "follow", "off"],
               ["look", id, "apply", "muted"], ["set", id, "brightness=5"]]
    var mic = all.filter(function(a) { return D.classify(a).kind === "mic" })
    compare(mic, [["look", id, "apply", "muted"], ["set", id, "brightness=5"]])
  }

  function test_describe() {
    compare(D.describeLook({ effect: "breathing", color: "00ff40", color2: "ffffff", speed: "", brightness: 255 }),
            "effect=breathing color=00ff40 color2=ffffff speed=- brightness=255")
    compare(D.describeLook({ effect: "", color: "", color2: "", speed: "", brightness: -1 }),
            "effect=- color=- color2=- speed=- brightness=-")
    compare(D.describeLook(null), "(none)")
    compare(D.describeAudio({ gain: 65, monitor: -1, monitorOn: null, headphones: 77 }),
            "gain=65% monitor=unknown monitor_on=unknown headphones=77%")
    compare(D.describeAudio({ gain: 0, monitor: 100, monitorOn: true, headphones: 0 }),
            "gain=0% monitor=100% monitor_on=yes headphones=0%")
  }

  function test_header() {
    var h = D.header({ when: new Date(2026, 9, 14, 12, 0, 0), path: "/x/pane-debug.txt", module: id,
                       periPath: "/x/peri", editing: "muted", online: true, follow: true,
                       looks: { unmuted: { effect: "wave", color: "", color2: "", speed: "fast", brightness: 40 },
                                muted: { effect: "", color: "", color2: "", speed: "", brightness: -1 } },
                       audio: { gain: 65, monitor: 86, monitorOn: false, headphones: 77 } })
    verify(h.indexOf("log file: /x/pane-debug.txt") >= 0)
    verify(h.indexOf("editing: the muted look") >= 0)
    verify(h.indexOf("follow mute: on") >= 0)
    verify(h.indexOf("unmuted look: effect=wave color=- color2=- speed=fast brightness=40") >= 0)
    verify(h.indexOf("muted look:   effect=- ") >= 0)
    verify(h.indexOf("audio: gain=65% monitor=86% monitor_on=no headphones=77%") >= 0)
    // Every tag the log uses is explained in the legend.
    D.legend.forEach(function(t) { verify(h.indexOf(t[0]) >= 0, t[0]) })
    compare(D.legend.map(function(t) { return t[0] }).indexOf("APPLY") >= 0, true)
  }
}
