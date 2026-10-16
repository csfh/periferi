#pragma once

#include <stdbool.h>
#include <stddef.h>

// Which XDG base a peri path hangs off. One module owns the rule, so a config
// path and a data path cannot drift:
//   CONFIG  $XDG_CONFIG_HOME, or $HOME/.config
//   DATA    $XDG_DATA_HOME, or $HOME/.local/share
//   RUNTIME $XDG_RUNTIME_DIR only. The spec gives it no default; inventing one
//           would put a socket where the other process is not listening.
// An empty or relative variable counts as unset (the basedir spec). The directory is not created.

enum peri_xdg {
    PERI_XDG_CONFIG,
    PERI_XDG_DATA,
    PERI_XDG_RUNTIME,
};

// Write "<base>/<rel>" into buf. A null or empty rel is the base itself.
// False if the base cannot be resolved or the result does not fit.
bool peri_xdg_path(char *buf, size_t size, enum peri_xdg base, const char *rel);
