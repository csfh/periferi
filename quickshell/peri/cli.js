.pragma library

// The argv for running peri: ["use", "drop-ctrl-v2"] -> a command for Process. peri is
// found at periPath, or else on PATH. The arguments are separate argv elements read
// through "$@", never pasted into the script, so no argument can be taken as shell code.
function command(periPath, args) {
  return ["sh", "-c", "p=\"$1\"; [ -x \"$p\" ] || p=peri; shift; exec \"$p\" \"$@\"",
          "sh", periPath].concat(args)
}
