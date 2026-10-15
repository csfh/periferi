.pragma library

// What a pane SHOWS and WRITES while the user's choices are on their way to a device, shared by every pane.
// Pure functions, no UI and no processes, so they can be tested on their own.
//
// The problem they solve: Omarchy's PanelSlider sets its knob back to the model's value the moment the mouse
// is released, and a write takes a moment (often a pause first, then the device, then a read back). A control
// bound to the model therefore jumps back and then forward again. And a read or a second choice that arrives
// while a write is on its way can overwrite or replace it. So a pane keeps:
//
//   shown   what the user just chose (key -> value), drawn INSTEAD of the model until the device has been
//           read again;
//   queue   the writes still to do, one entry per key (so a choice for one control never replaces another's),
//           each with the time it may be written;
//   holding how many sliders are being dragged (a new list of controls must not be put under them).

// The value to draw for a control: what the user just chose, else what the device last said.
function shownValue(key, value, shown) {
  return shown && Object.prototype.hasOwnProperty.call(shown, key) ? shown[key] : value
}

// A new `shown` with this choice in it (never changes the old one, so a binding sees the change).
function choose(shown, key, v) {
  var next = Object.assign({}, shown)
  next[key] = v
  return next
}

// A new `shown` without this key (the choice is no longer needed, or the write failed and the truth is drawn).
function omit(shown, key) {
  var next = Object.assign({}, shown)
  delete next[key]
  return next
}

// When the device has been read again: the choices it now reflects can go, but only if nothing is still on its
// way (a queued or running write, or a slider being held); otherwise the old value would flash back.
function settle(shown, queueLength, busy) {
  return queueLength === 0 && !busy ? {} : shown
}

// May the pane read the device now? Never while a choice is queued (the read would draw the old value over
// it), while a read or write runs, or while a slider is being dragged.
function mayRead(queueLength, busy, holding) {
  return queueLength === 0 && !busy && !holding
}

// ---- the write queue: one entry per key, in the order the user chose them ------------------------------------
// Each entry is { name, value, due } (due: when it may be written, a time in ms). Choosing the same key again
// replaces its entry in place; choosing another adds one, so no choice is ever dropped.

function enqueue(queue, name, value, due) {
  var next = queue.slice()
  for (var i = 0; i < next.length; i++) {
    if (next[i].name === name) { next[i] = { name: name, value: value, due: due }; return next }
  }
  next.push({ name: name, value: value, due: due })
  return next
}

// The queue without this key's entry (the user went back to the value the device already has).
function drop(queue, name) {
  return queue.filter(function(q) { return q.name !== name })
}

// The first entry whose time has come, and the rest of the queue: { entry, rest }, or null when none is due.
function dequeue(queue, now) {
  for (var i = 0; i < queue.length; i++) {
    if (queue[i].due <= now) {
      return { entry: queue[i], rest: queue.slice(0, i).concat(queue.slice(i + 1)) }
    }
  }
  return null
}

// Milliseconds until the earliest entry is due (at least 1), or 0 for an empty queue.
function wait(queue, now) {
  if (queue.length === 0) return 0
  var soonest = queue.reduce(function(m, q) { return Math.min(m, q.due) }, Infinity)
  return Math.max(1, soonest - now)
}
