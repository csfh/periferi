#include "xdg.h"

#include <stdio.h>
#include <stdlib.h>

bool peri_xdg_path(char *buf, size_t size, enum peri_xdg base, const char *rel)
{
    if (!buf || size == 0)
        return false;

    const char *env_name;
    const char *home_suffix; // under $HOME when the variable is unset; none for runtime
    switch (base) {
    case PERI_XDG_CONFIG:
        env_name = "XDG_CONFIG_HOME";
        home_suffix = ".config";
        break;
    case PERI_XDG_DATA:
        env_name = "XDG_DATA_HOME";
        home_suffix = ".local/share";
        break;
    case PERI_XDG_RUNTIME:
        env_name = "XDG_RUNTIME_DIR";
        home_suffix = nullptr;
        break;
    default:
        return false;
    }

    if (rel && !*rel)
        rel = nullptr;
    const char *env = getenv(env_name);
    int n;
    // The basedir spec: a relative path is invalid and is ignored, as if unset.
    // Used as it is, it would resolve from whatever directory the process is in.
    if (env && env[0] == '/')
        n = rel ? snprintf(buf, size, "%s/%s", env, rel) : snprintf(buf, size, "%s", env);
    else if (home_suffix) {
        const char *home = getenv("HOME");
        if (!home || !*home)
            return false;
        n = rel ? snprintf(buf, size, "%s/%s/%s", home, home_suffix, rel)
                : snprintf(buf, size, "%s/%s", home, home_suffix);
    } else
        return false;
    return n > 0 && (size_t)n < size;
}
