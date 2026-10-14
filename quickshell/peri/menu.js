.pragma library

// The device menu's logic, free of any UI or process code so it can be tested on its own.
// All matching of devices to modules is done by the daemon (the `status` and `module`
// fields of `peri list --json`); this file only joins and sorts what it reports.

// Parse the JSON a `peri ... --json` command printed. An array, or null if it is not one.
// (peri prints nothing on stdout when it fails, so empty text is a failure too.)
function parse(text) {
  try {
    var value = JSON.parse(text)
    return Array.isArray(value) ? value : null
  } catch (e) {
    return null
  }
}

function byName(a, b) {
  var an = String(a.name).toLowerCase(), bn = String(b.name).toLowerCase()
  if (an !== bn) return an < bn ? -1 : 1
  return String(a.key) < String(b.key) ? -1 : String(a.key) > String(b.key) ? 1 : 0
}

// The entry a detail pane shows: the one with this key, else the first, else null.
function selectEntry(list, key) {
  if (!Array.isArray(list) || list.length === 0) return null
  for (var i = 0; i < list.length; i++)
    if (list[i].key === key) return list[i]
  return list[0]
}

// `peri setup` has to update the udev rule before this installed module's device is covered.
// (The device may well work already: today's rule is broader. Only the new rule is missing.)
function needsSetup(module) {
  return !!module && module.access === "setup"
}

// The protocol engine of a module from `peri modules --json` ("xap", "razer"), "" if unknown.
function engineOf(module) {
  return module && module.engine ? module.engine : ""
}

// Do the keyboard controls (PeriLighting, `peri rgb`) belong to this entry? Only for the XAP
// engine, or when the engine is unknown (the list could not be read): the keyboard keeps its
// controls, while every other kind of device is never handed them.
function drivesKeyboard(entry) {
  return !!entry && (entry.engine === "xap" || entry.engine === "")
}

// devices: `peri list --json` (hidden ones are left out by peri, and skipped here again).
// modules: `peri modules --json`.
//
//   managed:     a device an installed module drives, or an installed module whose
//                device is not plugged in (connected: false, so it can still be removed)
//   installable: modules that are not installed; connected says their device is plugged in
//   unmanaged:   visible devices nothing can drive
//
// Every entry has `key` (unique and stable: the device's group, or "module:<id>"),
// `name`, `vid` and `pid`; managed and installable ones also have `module` / `id`, and
// managed ones `connected` and `setup` (`peri setup` must update the udev rule for them).
function build(devices, modules) {
  devices = Array.isArray(devices) ? devices : []
  modules = Array.isArray(modules) ? modules : []

  var known = {}       // module id -> module
  modules.forEach(function(m) { known[m.id] = m })

  var managed = [], installable = [], unmanaged = []
  var driven = {}      // installed module id -> a device is driving it
  var plugged = {}     // available module id -> its device is plugged in

  devices.forEach(function(d) {
    if (d.hidden) return
    var entry = { key: d.group, name: d.name, vid: d.vid, pid: d.pid, module: d.module,
                  engine: engineOf(known[d.module]) }
    if (d.status === "installed") {
      driven[d.module] = true
      managed.push(Object.assign({ connected: true, setup: needsSetup(known[d.module]) }, entry))
    } else if (d.status === "available" && known[d.module]) {
      plugged[d.module] = true
    } else {
      unmanaged.push(entry) // none, or "available" for a module the list does not know
    }
  })

  modules.forEach(function(m) {
    if (m.status === "installed" && !driven[m.id]) {
      managed.push({ key: "module:" + m.id, name: m.name, vid: m.vid, pid: m.pid,
                     module: m.id, engine: engineOf(m), connected: false, setup: needsSetup(m) })
    } else if (m.status === "available") {
      installable.push({ key: "module:" + m.id, id: m.id, name: m.name, version: m.version,
                         vid: m.vid, pid: m.pid, engine: engineOf(m),
                         connected: plugged[m.id] === true })
    }
  })

  return {
    managed: managed.sort(byName),
    installable: installable.sort(byName),
    unmanaged: unmanaged.sort(byName)
  }
}
