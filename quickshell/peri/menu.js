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

// `peri setup` has to update the udev rule before this installed module's device is covered.
// (The device may well work already: today's rule is broader. Only the new rule is missing.)
function needsSetup(module) {
  return !!module && module.access === "setup"
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
    var entry = { key: d.group, name: d.name, vid: d.vid, pid: d.pid, module: d.module }
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
                     module: m.id, connected: false, setup: needsSetup(m) })
    } else if (m.status === "available") {
      installable.push({ key: "module:" + m.id, id: m.id, name: m.name, version: m.version,
                         vid: m.vid, pid: m.pid, connected: plugged[m.id] === true })
    }
  })

  return {
    managed: managed.sort(byName),
    installable: installable.sort(byName),
    unmanaged: unmanaged.sort(byName)
  }
}

// The argv for running peri: ["use", "drop-ctrl-v2"] -> a command for Process. peri is
// found at periPath, or else on PATH. The arguments are separate argv elements read
// through "$@", never pasted into the script, so no argument can be taken as shell code.
function command(periPath, args) {
  return ["sh", "-c", "p=\"$1\"; [ -x \"$p\" ] || p=peri; shift; exec \"$p\" \"$@\"",
          "sh", periPath].concat(args)
}
