import QtQuick
import QtTest
import "../../quickshell/peri/choices.js" as Ch

TestCase {
  name: "choices"

  // What the pane draws: the user's choice until the device has been read again.
  function test_shown_value() {
    compare(Ch.shownValue("brightness", 128, {}), 128)
    compare(Ch.shownValue("brightness", 128, null), 128)
    compare(Ch.shownValue("brightness", 128, { brightness: 160 }), 160)
    compare(Ch.shownValue("contrast", 128, { brightness: 160 }), 128)         // other keys are untouched
    compare(Ch.shownValue("brightness", 128, { brightness: 0 }), 0)            // 0 is a real choice
    compare(Ch.shownValue("on", true, { on: false }), false)                   // so is false
  }

  function test_choose_never_changes_the_old_object() {
    var older = {}
    var newer = Ch.choose(older, "gain", 7)
    compare(Object.keys(older).length, 0)
    compare(newer.gain, 7)
    compare(Ch.choose(newer, "gain", 9).gain, 9)
    compare(newer.gain, 7)
  }

  function test_omit() {
    var shown = { dpi: 1600, poll: 500 }
    var next = Ch.omit(shown, "dpi")
    compare(Object.keys(next), ["poll"])
    compare(shown.dpi, 1600)                       // the old object is never changed
    compare(Object.keys(Ch.omit(shown, "nothing")).length, 2)
  }

  // Only settle when nothing is on its way, or the old value would flash back.
  function test_settle_and_may_read() {
    var shown = { brightness: 160 }
    compare(Object.keys(Ch.settle(shown, 0, false)).length, 0)
    compare(Ch.settle(shown, 1, false), shown)
    compare(Ch.settle(shown, 0, true), shown)
    compare(Ch.mayRead(0, false, false), true)
    compare(Ch.mayRead(1, false, false), false)
    compare(Ch.mayRead(0, true, false), false)
    compare(Ch.mayRead(0, false, true), false)
  }

  // The write queue never drops a choice: one entry per key, in the order they were chosen.
  function test_queue() {
    var q = Ch.enqueue([], "brightness", 150, 600)
    q = Ch.enqueue(q, "saturation", 200, 700)
    compare(q.length, 2)
    compare(q[0].name, "brightness"); compare(q[1].name, "saturation")
    q = Ch.enqueue(q, "brightness", 151, 900)           // the same key again replaces its entry in place
    compare(q.length, 2); compare(q[0].value, 151); compare(q[0].due, 900); compare(q[1].name, "saturation")
    var before = Ch.enqueue([], "gain", 1, 10)
    Ch.enqueue(before, "contrast", 2, 20)                // the old queue is never changed
    compare(before.length, 1)
    compare(Ch.drop(q, "brightness").length, 1)
    compare(Ch.drop(q, "nothing").length, 2)
  }

  function test_queue_hands_out_what_is_due() {
    var q = Ch.enqueue(Ch.enqueue([], "brightness", 150, 600), "saturation", 200, 100)
    compare(Ch.dequeue(q, 50), null)
    var hit = Ch.dequeue(q, 100)
    compare(hit.entry.name, "saturation"); compare(hit.rest.length, 1)
    compare(Ch.dequeue(hit.rest, 599), null)
    compare(Ch.dequeue(hit.rest, 600).entry.name, "brightness")
    compare(Ch.wait(q, 0), 100)
    compare(Ch.wait(q, 100), 1)
    compare(Ch.wait([], 0), 0)
  }
}
