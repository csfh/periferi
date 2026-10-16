#pragma once

#include <stdbool.h>
#include <stddef.h>

// The systemd user unit that runs perid. config_home and data_home are the
// absolute directories `peri setup` resolved (the XDG bases, not peri/...).
// They are written into the unit so the daemon uses those and not the user
// manager's defaults, which can differ from the shell that installed the
// modules. The runtime directory is left unset: it is per session and the
// spec gives it no default.
//
// False (and buf empty, when buf is non-null and size > 0) if any path is
// missing, not absolute, or contains a character a unit file would treat
// specially (a quote, a backslash, a % or $, or a control byte). A path that does
// not fit is refused rather than written cut off.
bool peri_user_unit(char *buf, size_t size, const char *perid_path,
                    const char *config_home, const char *data_home);
