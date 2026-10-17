import QtQuick
import QtTest
import "../../quickshell/peri/mic.js" as Mic

TestCase {
  name: "mic"

  // What `peri status` and `peri look ... get` print (shapes copied from the real daemon).
  readonly property string status: "module   razer-seiren-v3-chroma\nfirmware 0101\nbrightness 102\nmute     muted\ndial     43\n"
  function lookText(effect, color, brightness, color2, speed) {
    return "module   razer-seiren-v3-chroma\nlook     unmuted\neffect   " + effect
      + "\ncolor    " + color + "\ncolor2   " + (color2 || "none") + "\nspeed    " + (speed || "default")
      + "\nbrightness " + brightness + "\n"
  }
  function look(effect, color, brightness, color2, speed) {
    return { effect: effect, color: color, color2: color2 || "", speed: speed || "", brightness: brightness }
  }

  // --- reading `peri status`: online, and the mic's own brightness ---------------------------
  function test_parse_status() {
    var s = Mic.parseStatus(status)
    compare(s.online, true)
    compare(s.brightness, 102)
  }

  // A refusal or nothing at all means offline: a pane must never show stale numbers as live.
  function test_offline_when_not_a_full_answer() {
    compare(Mic.parseStatus("").online, false)
    compare(Mic.parseStatus("razer-seiren-v3-chroma: the device is not connected\n").online, false)
    compare(Mic.parseStatus("module m\nfirmware 0101\n").online, false)           // no brightness
    compare(Mic.parseStatus("module m\nbrightness 300\n").online, false)           // out of range
    compare(Mic.parseStatus("module m\nbrightness abc\n").online, false)
    compare(Mic.parseStatus("brightness 10\n").online, false)                       // no module
    compare(Mic.parseStatus(undefined).online, false)
  }

  // --- reading a saved look ------------------------------------------------------------------
  function test_parse_look() {
    compare(Mic.parseLook(lookText("static", "0000FF", "102")), look("static", "0000ff", 102))
    compare(Mic.parseLook(lookText("breathing", "none", "40")), look("breathing", "", 40))
    compare(Mic.parseLook(lookText("none", "none", "0")), look("none", "", 0)) // Off is a real effect
    compare(Mic.parseLook(lookText("unset", "none", "unset")), look("", "", -1))
    compare(Mic.parseLook(lookText("spectrum", "none", "unset")), look("spectrum", "", -1))
    compare(Mic.parseLook(lookText("wave-reverse", "none", "unset")), look("wave-reverse", "", -1))
    compare(Mic.parseLook(lookText("breathing", "00ff00", "80", "FF0000")),
            look("breathing", "00ff00", 80, "ff0000"))
    compare(Mic.parseLook(lookText("starlight", "ff0000", "unset")), look("", "", -1)) // no longer an effect here
  }

  // Bad or missing fields spoil only themselves, and a failed read is not an empty look.
  function test_parse_look_bad_fields() {
    compare(Mic.parseLook(lookText("rainbow", "none", "70")), look("", "", 70))
    compare(Mic.parseLook(lookText("static", "none", "70")), look("", "", 70))    // static needs a colour
    compare(Mic.parseLook(lookText("static", "zz", "70")), look("", "", 70))
    compare(Mic.parseLook(lookText("wave", "0000ff", "70")), look("wave", "", 70)) // a wave has no colour
    compare(Mic.parseLook(lookText("breathing", "none", "70", "ff0000")), look("breathing", "", 70)) // no first colour
    compare(Mic.parseLook(lookText("static", "0000ff", "70", "ff0000")), look("static", "0000ff", 70)) // static takes one
    compare(Mic.parseLook(lookText("none", "none", "300")), look("none", "", -1))
    compare(Mic.parseLook(lookText("none", "none", "abc")), look("none", "", -1))
    compare(Mic.parseLook(""), null)
    compare(Mic.parseLook("razer-seiren-v3-chroma: the static effect needs a color=<rrggbb>\n"), null)
    compare(Mic.parseLook("module m\n"), null)                                      // no look line
    compare(Mic.parseLook(undefined), null)
  }

  // --- editing a look: whole-look results, never merged on the daemon -----------------------
  function test_with_effect_keeps_what_the_effect_can_use() {
    var two = look("breathing", "00ff00", 40, "ff0000")
    compare(Mic.withEffect(two, "breathing"), look("breathing", "00ff00", 40, "ff0000"))  // both kept
    compare(Mic.withEffect(two, "static"), look("static", "00ff00", 40))                   // one kept
    compare(Mic.withEffect(two, "wave"), look("wave", "", 40))                             // none kept
    compare(Mic.withEffect(look("wave", "", 40), "breathing"), look("breathing", "", 40))  // random colours
    compare(Mic.withEffect(look("", "", -1), "none"), look("none", "", -1))
    compare(Mic.withEffect(look("breathing", "", -1), "static"), look("static", "ffffff", -1)) // needs one: white
  }

  function test_bad_effects_are_not_built() {
    compare(Mic.withEffect(look("", "", -1), "rainbow"), null)
    compare(Mic.withEffect(look("", "", -1), ""), null)
    compare(Mic.withEffect(look("", "", -1), "fire"), null)
  }

  function test_with_color() {
    // A swatch in the first row colours the current effect, or switches to Colour.
    compare(Mic.withColor(look("breathing", "", 40), 1, "FF0000"), look("breathing", "ff0000", 40))
    compare(Mic.withColor(look("static", "0000ff", -1), 1, "ff0000"), look("static", "ff0000", -1))
    compare(Mic.withColor(look("spectrum", "", 40), 1, "ff0000"), look("static", "ff0000", 40))
    compare(Mic.withColor(look("", "", -1), 1, "ff0000"), look("static", "ff0000", -1))
    compare(Mic.withColor(look("wave", "", -1), 1, "00ff00"), look("static", "00ff00", -1))
    // A first colour keeps a second one only where there can be two.
    compare(Mic.withColor(look("breathing", "00ff00", -1, "ff0000"), 1, "0000ff"),
            look("breathing", "0000ff", -1, "ff0000"))
    // The second row: only for an effect that takes two and has a first.
    compare(Mic.withColor(look("breathing", "00ff00", -1), 2, "ff0000"), look("breathing", "00ff00", -1, "ff0000"))
    compare(Mic.withColor(look("breathing", "", -1), 2, "ff0000"), null)
    compare(Mic.withColor(look("static", "0000ff", -1), 2, "ff0000"), null)
    compare(Mic.withColor(look("wave", "", -1), 2, "ff0000"), null)
    // Bad colours and slots.
    compare(Mic.withColor(look("breathing", "", -1), 1, "zz0000"), null)
    compare(Mic.withColor(look("breathing", "", -1), 1, "00ff"), null)
    compare(Mic.withColor(look("breathing", "", -1), 1, "0000ff; rm"), null)
    compare(Mic.withColor(look("breathing", "", -1), 3, "0000ff"), null)
  }

  function test_random_colours() {
    compare(Mic.withRandomColors(look("breathing", "00ff00", 40, "ff0000")), look("breathing", "", 40))
    compare(Mic.withRandomColors(look("static", "0000ff", -1)), null)    // static must keep its colour
    compare(Mic.withRandomColors(look("wave", "", -1)), null)
    compare(Mic.withRandomColors(look("", "", -1)), null)
    verify(Mic.showRandomColors(look("breathing", "", -1)))
    verify(!Mic.showRandomColors(look("static", "ff0000", -1)))
    verify(!Mic.showRandomColors(null))
    verify(Mic.showSecondColor(look("breathing", "ff0000", -1)))
    verify(!Mic.showSecondColor(look("breathing", "", -1)))              // needs a first
    verify(!Mic.showSecondColor(look("static", "ff0000", -1)))
    verify(!Mic.showSecondColor(null))
  }

  // Swatches are hidden for effects that take no colour, and shown where they matter.
  function test_colours_are_hidden_for_effects_without_them() {
    verify(!Mic.showColors(look("spectrum", "", -1)))
    verify(!Mic.showColors(look("wave", "", -1)))
    verify(!Mic.showColors(look("wave-reverse", "", -1)))
    verify(!Mic.showColors(look("none", "", -1)))
    verify(Mic.showColors(look("static", "0000ff", -1)))
    verify(Mic.showColors(look("breathing", "", -1)))
    verify(Mic.showColors(look("", "", -1)))     // nothing chosen yet: a swatch starts the Colour effect
    verify(Mic.showColors(null))
  }

  // --- wave speed -----------------------------------------------------------------------------
  function test_parse_look_speed() {
    compare(Mic.parseLook(lookText("wave", "none", "unset", "none", "fast")), look("wave", "", -1, "", "fast"))
    compare(Mic.parseLook(lookText("wave-reverse", "none", "40", "none", "slow")), look("wave-reverse", "", 40, "", "slow"))
    compare(Mic.parseLook(lookText("wave", "none", "unset")), look("wave", "", -1))             // "default": not saved
    compare(Mic.parseLook(lookText("spectrum", "none", "unset", "none", "fast")), look("spectrum", "", -1)) // no speed there
    compare(Mic.parseLook(lookText("wave", "none", "unset", "none", "warp")), look("wave", "", -1))        // not a speed
  }

  function test_with_speed() {
    compare(Mic.withSpeed(look("wave", "", 40), "fast"), look("wave", "", 40, "", "fast"))
    compare(Mic.withSpeed(look("wave-reverse", "", -1, "", "fast"), "slow"), look("wave-reverse", "", -1, "", "slow"))
    compare(Mic.withSpeed(look("spectrum", "", -1), "fast"), null)       // no speed for this effect
    compare(Mic.withSpeed(look("", "", -1), "fast"), null)
    compare(Mic.withSpeed(look("wave", "", -1), "warp"), null)
    compare(Mic.withSpeed(look("wave", "", -1), ""), null)
  }

  // The speed stays across the two waves and is dropped for any other effect.
  function test_speed_follows_the_effect() {
    var fast = look("wave", "", 40, "", "fast")
    compare(Mic.withEffect(fast, "wave-reverse"), look("wave-reverse", "", 40, "", "fast"))
    compare(Mic.withEffect(fast, "spectrum"), look("spectrum", "", 40))
    compare(Mic.withEffect(look("spectrum", "", 40), "wave"), look("wave", "", 40))
    compare(Mic.withBrightness(fast, 100), look("wave", "", 255, "", "fast"))
    compare(Mic.withColor(fast, 1, "ff0000"), look("static", "ff0000", 40))   // a colour switches to Colour: no speed
  }

  function test_speed_in_the_save_arguments_and_texts() {
    compare(Mic.saveArgs("unmuted", look("wave", "", 40, "", "fast")),
            ["save", "unmuted", "effect=wave", "speed=fast", "brightness=40"])
    compare(Mic.saveArgs("unmuted", look("wave", "", -1)), ["save", "unmuted", "effect=wave"])
    compare(Mic.saveArgs("unmuted", look("spectrum", "", -1, "", "fast")), ["save", "unmuted", "effect=spectrum"]) // stray speed left out
    verify(Mic.showSpeed(look("wave", "", -1)) && Mic.showSpeed(look("wave-reverse", "", -1)))
    verify(!Mic.showSpeed(look("spectrum", "", -1)) && !Mic.showSpeed(look("", "", -1)) && !Mic.showSpeed(null))
    verify(Mic.speedSelected("fast", look("wave", "", -1, "", "fast")))
    verify(!Mic.speedSelected("slow", look("wave", "", -1, "", "fast")))
    verify(Mic.speedSelected("slow", look("wave", "", -1)))              // nothing saved: the slow one
    verify(!Mic.speedSelected("fast", look("wave", "", -1)))
    verify(!Mic.speedSelected("slow", look("spectrum", "", -1)))
    compare(Mic.lookText(look("wave", "", -1, "", "fast")), "Saved: wave, fast")
    compare(Mic.lookText(look("wave-reverse", "", 51)), "Saved: wave, other way, brightness 20%")
    compare(Mic.speeds.map(function(x) { return x.id }), ["slow", "fast"])
  }

  function test_with_brightness() {
    compare(Mic.withBrightness(look("static", "0000ff", 0), 40), look("static", "0000ff", 102))
    compare(Mic.withBrightness(look("", "", -1), 100), look("", "", 255))
    compare(Mic.withBrightness(look("", "", -1), 150), look("", "", 255))   // clamped
    compare(Mic.withBrightness(look("", "", 5), -20), look("", "", 0))
    compare(Mic.withBrightness(look("", "", 5), "junk"), look("", "", 0))
    compare(Mic.withBrightness(look("breathing", "00ff00", 5, "ff0000"), 40),
            look("breathing", "00ff00", 102, "ff0000"))                        // colours kept
  }

  // A release that does not move the value saves nothing.
  function test_an_unchanged_brightness_is_not_saved() {
    compare(Mic.withBrightness(look("none", "", 102), 40), null)
    compare(Mic.withBrightness(look("", "", 255), 100), null)
  }

  // --- the arguments of `peri look` ---------------------------------------------------------
  function test_save_args() {
    compare(Mic.saveArgs("unmuted", look("static", "0000ff", 102)),
            ["save", "unmuted", "effect=static", "color=0000ff", "brightness=102"])
    compare(Mic.saveArgs("unmuted", look("breathing", "", -1)), ["save", "unmuted", "effect=breathing"])
    compare(Mic.saveArgs("unmuted", look("breathing", "00ff00", -1, "ff0000")),
            ["save", "unmuted", "effect=breathing", "color=00ff00", "color2=ff0000"])
    compare(Mic.saveArgs("unmuted", look("wave-reverse", "", 10)), ["save", "unmuted", "effect=wave-reverse", "brightness=10"])
    compare(Mic.saveArgs("unmuted", look("none", "", 0)), ["save", "unmuted", "effect=none", "brightness=0"])
    compare(Mic.saveArgs("unmuted", look("", "", 40)), ["save", "unmuted", "brightness=40"])
    compare(Mic.saveArgs("unmuted", look("", "", -1)), ["save", "unmuted"])      // clears the look
    compare(Mic.saveArgs("unmuted", look("wave", "0000ff", -1)), ["save", "unmuted", "effect=wave"]) // stray colour left out
    compare(Mic.saveArgs("unmuted", look("breathing", "", -1, "ff0000")), ["save", "unmuted", "effect=breathing"]) // second needs a first
    compare(Mic.applyArgs("muted"), ["apply", "muted"])
  }

  function test_can_apply() {
    verify(Mic.canApply(look("none", "", -1)))
    verify(Mic.canApply(look("", "", 0)))
    verify(!Mic.canApply(look("", "", -1)))
    verify(!Mic.canApply(null))
  }

  // --- the highlight follows the saved look -------------------------------------------------
  function test_selection_follows_the_saved_look() {
    var spec = look("spectrum", "", -1), blue = look("static", "0000ff", 10), none = look("", "", -1)
    var two = look("breathing", "00ff00", -1, "ff0000")
    verify(Mic.effectSelected("spectrum", spec))
    verify(!Mic.effectSelected("breathing", spec))
    verify(!Mic.effectSelected("spectrum", none))
    verify(!Mic.effectSelected("spectrum", null))
    verify(Mic.swatchSelected("0000ff", blue, 1))
    verify(Mic.swatchSelected("0000FF", blue, 1))
    verify(!Mic.swatchSelected("ff0000", blue, 1))
    verify(!Mic.swatchSelected("0000ff", spec, 1))      // no colour saved: no swatch is lit
    verify(!Mic.swatchSelected("0000ff", none, 1))
    verify(!Mic.swatchSelected("bad", blue, 1))
    verify(Mic.swatchSelected("00ff00", two, 1) && !Mic.swatchSelected("ff0000", two, 1))
    verify(Mic.swatchSelected("ff0000", two, 2) && !Mic.swatchSelected("00ff00", two, 2))
  }

  // --- texts ---------------------------------------------------------------------------------
  function test_texts() {
    compare(Mic.lookText(look("", "", -1)), "Nothing saved in this look yet")
    compare(Mic.lookText(null), "Nothing saved in this look yet")
    compare(Mic.lookText(look("static", "0000ff", 102)), "Saved: colour 0000ff, brightness 40%")
    compare(Mic.lookText(look("none", "", -1)), "Saved: off")
    compare(Mic.lookText(look("", "", 255)), "Saved: brightness 100%")
    compare(Mic.lookText(look("breathing", "", -1)), "Saved: breathing, random colours")
    compare(Mic.lookText(look("breathing", "00ff00", -1)), "Saved: breathing, colour 00ff00")
    compare(Mic.lookText(look("breathing", "00ff00", 51, "ff0000")),
            "Saved: breathing, colours 00ff00 and ff0000, brightness 20%")
    compare(Mic.lookText(look("wave-reverse", "", -1)), "Saved: wave, other way")
    compare(Mic.lookLabel("unmuted"), "Unmuted look")
    compare(Mic.lookLabel("muted"), "Muted look")
    compare(Mic.lookLabel("other"), "")
  }

  function test_fixed_lists() {
    Mic.presets.forEach(function(p) { verify(Mic.validHex(p.hex), p.name) })
    compare(Mic.presets.length, 8)
    compare(Mic.effects.map(function(e) { return e.id }),
            ["static", "breathing", "spectrum", "wave", "wave-reverse", "none"])
    compare(Mic.looks.map(function(l) { return l.id }), ["unmuted", "muted"])
    Mic.effects.forEach(function(e) {
      verify(Mic.withEffect(look("", "", -1), e.id) !== null, e.id)
      compare(Mic.maxColors(e.id) === e.colors, true)
    })
    compare(Mic.maxColors("static"), 1)
    compare(Mic.maxColors("breathing"), 2)
    compare(Mic.maxColors("wave"), 0)
    compare(Mic.maxColors("nope"), 0)
  }

  // --- audio ---------------------------------------------------------------------------------
  readonly property string audioText: "module   razer-seiren-v3-chroma\ngain     65\nmonitor  86\nmonitor_on no\nheadphones 77\n"

  function test_parse_audio() {
    compare(Mic.parseAudio(audioText), { gain: 65, monitor: 86, monitorOn: false, headphones: 77 })
    compare(Mic.parseAudio("module m\ngain 0\nmonitor 100\nmonitor_on yes\nheadphones 5\n"),
            { gain: 0, monitor: 100, monitorOn: true, headphones: 5 })
    // A control the card did not report is unknown, not zero.
    compare(Mic.parseAudio("module m\ngain unknown\nmonitor_on unknown\n"),
            { gain: -1, monitor: -1, monitorOn: null, headphones: -1 })
    compare(Mic.parseAudio("module m\ngain 101\nheadphones -3\n").gain, -1)
    compare(Mic.parseAudio(""), null)
    compare(Mic.parseAudio("razer-seiren-v3-chroma: the sound card is not connected\n"), null)
    compare(Mic.parseAudio(undefined), null)
  }

  function test_audio_setting() {
    var cur = { gain: 65, monitor: 86, monitorOn: false, headphones: 77 }
    compare(Mic.audioSetting("gain", 40, cur), "gain=40")
    compare(Mic.audioSetting("monitor", 0, cur), "monitor=0")
    compare(Mic.audioSetting("headphones", 99.6, cur), "headphones=100")   // rounded
    compare(Mic.audioSetting("gain", 150, cur), "gain=100")                // clamped
    compare(Mic.audioSetting("gain", -4, cur), "gain=0")
    compare(Mic.audioSetting("gain", "junk", cur), "gain=0")
    compare(Mic.audioSetting("monitor_on", true, cur), "monitor_on=yes")
    compare(Mic.audioSetting("gain", 40, null), "gain=40")                 // nothing known yet
  }

  // A release that does not move the value sends nothing, and only the daemon's keys are built.
  function test_audio_setting_refusals() {
    var cur = { gain: 65, monitor: 86, monitorOn: false, headphones: 77 }
    compare(Mic.audioSetting("gain", 65, cur), null)
    compare(Mic.audioSetting("gain", 64.6, cur), null)
    compare(Mic.audioSetting("monitor_on", false, cur), null)
    compare(Mic.audioSetting("monitor_on", "yes", cur), null)              // must be a boolean
    compare(Mic.audioSetting("brightness", 10, cur), null)
    compare(Mic.audioSetting("", 10, cur), null)
    compare(Mic.audioSetting("gain; rm", 10, cur), null)
  }

  function test_with_audio() {
    var cur = { gain: 65, monitor: 86, monitorOn: false, headphones: 77 }
    compare(Mic.withAudio(cur, "gain", 40.4), { gain: 40, monitor: 86, monitorOn: false, headphones: 77 })
    compare(Mic.withAudio(cur, "monitor_on", true), { gain: 65, monitor: 86, monitorOn: true, headphones: 77 })
    compare(Mic.withAudio(cur, "headphones", 500).headphones, 100)
    compare(Mic.withAudio(cur, "other", 1), cur)
    compare(Mic.withAudio(cur, "monitor_on", "yes"), cur)
    compare(cur.gain, 65)                                   // the input is never changed
    compare(Mic.withAudio(null, "gain", 5).gain, 5)
    compare(Mic.withAudio(null, "gain", 5).monitorOn, null)
  }
  // --- the Muted look: the colour the firmware shows while muted, a static colour only --------
  function test_muted_look() {
    compare(Mic.isMuted("muted"), true)
    compare(Mic.isMuted("unmuted"), false)
    compare(Mic.mutedColor("FF8000"), look("static", "ff8000", -1))
    compare(Mic.mutedColor("zz"), null)
    compare(Mic.mutedColor(undefined), null)

    // Applying needs the static colour; the Unmuted look keeps its own rule.
    compare(Mic.canApplyFor("muted", look("static", "ff0000", -1)), true)
    compare(Mic.canApplyFor("muted", look("static", "ff0000", 38)), true)   // an old brightness is not sent
    compare(Mic.canApplyFor("muted", look("wave", "", -1)), false)
    compare(Mic.canApplyFor("muted", look("", "", 40)), false)
    compare(Mic.canApplyFor("muted", look("", "", -1)), false)
    compare(Mic.canApplyFor("muted", null), false)
    compare(Mic.canApplyFor("unmuted", look("wave", "", -1)), true)
    compare(Mic.canApplyFor("unmuted", look("", "", 40)), true)
    compare(Mic.canApplyFor("unmuted", look("", "", -1)), false)

    // Saving it sends a colour and nothing else, so the daemon never has to refuse the pane.
    compare(Mic.saveArgs("muted", look("static", "ff0000", 38)), ["save", "muted", "effect=static", "color=ff0000"])
    compare(Mic.saveArgs("muted", look("wave", "", 20)), ["save", "muted"])
    compare(Mic.saveArgs("unmuted", look("static", "ff0000", 38)), ["save", "unmuted", "effect=static", "color=ff0000", "brightness=38"])

    // The Muted tab is swatches only.
    compare(Mic.showColorsFor("muted", look("wave", "", -1)), true)
    compare(Mic.showColorsFor("unmuted", look("wave", "", -1)), false)
    compare(Mic.showEffectControls("muted"), false)
    compare(Mic.showEffectControls("unmuted"), true)
    compare(Mic.showBrightness("muted"), false)
    compare(Mic.showBrightness("unmuted"), true)
    compare(Mic.mutedText(look("static", "ff8000", -1)), "Saved: colour ff8000")
    compare(Mic.mutedText(look("", "", -1)), "Nothing saved in this look yet")
    compare(Mic.mutedText(look("wave", "", -1)), "Nothing saved in this look yet")
  }
}
