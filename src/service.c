#include "service.h"

#include <stdio.h>

// A path that can sit inside a double-quoted unit assignment. Absolute, so a
// relative XDG value is not resolved later from the daemon's working
// directory. No quotes or backslashes (they break the quoting) and no %
// (systemd expands specifiers such as %h in Environment=).
static bool unit_path_ok(const char *path)
{
    if (!path || path[0] != '/')
        return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        if (*p < 0x20 || *p == 0x7f || *p == '"' || *p == '\\' || *p == '%')
            return false;
    return true;
}

bool peri_user_unit(char *buf, size_t size, const char *perid_path,
                    const char *config_home, const char *data_home)
{
    if (buf && size > 0)
        buf[0] = '\0';
    if (!buf || size == 0 || !unit_path_ok(perid_path) || !unit_path_ok(config_home)
        || !unit_path_ok(data_home))
        return false;

    int n = snprintf(buf, size,
                     "[Unit]\n"
                     "Description=peri peripheral daemon\n"
                     "\n"
                     "[Service]\n"
                     "ExecStart=\"%s\"\n"
                     "# Directories `peri setup` resolved. A user service does not see the\n"
                     "# shell's XDG_* variables, so these pin the ones the modules were\n"
                     "# installed under. The runtime directory stays the session's own.\n"
                     "Environment=\"XDG_CONFIG_HOME=%s\"\n"
                     "Environment=\"XDG_DATA_HOME=%s\"\n"
                     "Restart=on-failure\n"
                     "\n"
                     "[Install]\n"
                     "WantedBy=default.target\n",
                     perid_path, config_home, data_home);
    if (n < 0 || (size_t)n >= size) {
        buf[0] = '\0';
        return false;
    }
    return true;
}
