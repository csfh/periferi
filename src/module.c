#include "module.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fsutil.h"
#include "scan.h"
#include "tomlc17.h"

static const size_t max_module_file = 64 * 1024;

bool peri_module_id_valid(const char *id)
{
    size_t len = strlen(id);
    if (len == 0 || len > 48)
        return false;
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        bool edge = i == 0 || i == len - 1;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && !edge)))
            return false;
    }
    return true;
}

// Writes the message to err and returns false, so callers can `return fail(...)`.
[[gnu::format(printf, 3, 4)]]
static bool fail(char *err, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, size, fmt, ap);
    va_end(ap);
    return false;
}

static bool get_string(toml_datum_t top, const char *key, char *out, size_t cap,
                       char *err, size_t err_size)
{
    toml_datum_t d = toml_seek(top, key);
    if (d.type == TOML_UNKNOWN)
        return fail(err, err_size, "missing '%s'", key);
    if (d.type != TOML_STRING)
        return fail(err, err_size, "'%s' must be a string", key);
    if (d.u.str.len == 0 || (size_t)d.u.str.len >= cap)
        return fail(err, err_size, "'%s' must be 1 to %zu characters", key, cap - 1);
    memcpy(out, d.u.str.ptr, (size_t)d.u.str.len);
    out[d.u.str.len] = '\0';
    return true;
}

// An integer 0-0xffff. If absent: an error when required, else *present = false.
static bool get_u16(toml_datum_t top, const char *key, bool required, uint16_t *out,
                    bool *present, char *err, size_t err_size)
{
    toml_datum_t d = toml_seek(top, key);
    *present = d.type != TOML_UNKNOWN;
    if (!*present)
        return required ? fail(err, err_size, "missing '%s'", key) : true;
    if (d.type != TOML_INT64 || d.u.int64 < 0 || d.u.int64 > 0xFFFF)
        return fail(err, err_size, "'%s' must be an integer from 0 to 0xffff", key);
    *out = (uint16_t)d.u.int64;
    return true;
}

static bool parse_fields(toml_datum_t top, struct peri_module *m, char *err, size_t err_size)
{
    toml_datum_t schema = toml_seek(top, "schema");
    if (schema.type != TOML_INT64 || schema.u.int64 != 1)
        return fail(err, err_size, "'schema' must be 1");

    if (!get_string(top, "id", m->id, sizeof m->id, err, err_size)
        || !get_string(top, "name", m->name, sizeof m->name, err, err_size)
        || !get_string(top, "version", m->version, sizeof m->version, err, err_size)
        || !get_string(top, "engine", m->engine, sizeof m->engine, err, err_size))
        return false;
    if (!peri_module_id_valid(m->id))
        return fail(err, err_size, "'id' must be lowercase letters, digits and '-'");
    if (strcmp(m->engine, "xap") != 0 && strcmp(m->engine, "razer") != 0
        && strcmp(m->engine, "razer90") != 0)
        return fail(err, err_size, "unknown engine '%s'", m->engine);

    bool present, has_page, has_usage;
    if (!get_u16(top, "match.vid", true, &m->vid, &present, err, err_size)
        || !get_u16(top, "match.pid", true, &m->pid, &present, err, err_size)
        || !get_u16(top, "match.usage_page", false, &m->usage_page, &has_page, err, err_size)
        || !get_u16(top, "match.usage", false, &m->usage, &has_usage, err, err_size))
        return false;
    if (m->vid == 0)
        return fail(err, err_size, "'match.vid' must not be 0");
    if (has_page != has_usage)
        return fail(err, err_size, "'match.usage_page' and 'match.usage' go together");
    m->has_usage = has_page;
    // Every engine talks to one interface of a device, so a module must name it. It is not always
    // a vendor page: the DeathAdder's control item sits inside its mouse collection (0001:0002).
    if (!m->has_usage)
        return fail(err, err_size, "engine '%s' needs 'match.usage_page' and 'match.usage'",
                    m->engine);

    toml_datum_t rgb = toml_seek(top, "capabilities.rgb");
    if (rgb.type != TOML_UNKNOWN && rgb.type != TOML_BOOLEAN)
        return fail(err, err_size, "'capabilities.rgb' must be true or false");
    m->rgb = rgb.type == TOML_BOOLEAN && rgb.u.boolean;

    toml_datum_t max = toml_seek(top, "limits.max_brightness");
    if (max.type != TOML_UNKNOWN) {
        if (max.type != TOML_INT64 || max.u.int64 < 1 || max.u.int64 > 255)
            return fail(err, err_size, "'limits.max_brightness' must be an integer from 1 to 255");
        m->max_brightness = (uint8_t)max.u.int64;
    }
    toml_datum_t min_dpi = toml_seek(top, "limits.min_dpi");
    toml_datum_t max_dpi = toml_seek(top, "limits.max_dpi");
    if ((min_dpi.type == TOML_UNKNOWN) != (max_dpi.type == TOML_UNKNOWN))
        return fail(err, err_size, "'limits.min_dpi' and 'limits.max_dpi' go together");
    if (min_dpi.type != TOML_UNKNOWN) {
        if (min_dpi.type != TOML_INT64 || max_dpi.type != TOML_INT64 || min_dpi.u.int64 < 1
            || max_dpi.u.int64 > 65535 || min_dpi.u.int64 > max_dpi.u.int64)
            return fail(err, err_size,
                        "'limits.min_dpi' and 'limits.max_dpi' must be integers, min at least 1, "
                        "max at most 65535 and not below min");
        m->min_dpi = (uint16_t)min_dpi.u.int64;
        m->max_dpi = (uint16_t)max_dpi.u.int64;
    }
    return true;
}

bool peri_module_parse(const char *text, size_t len, struct peri_module *m,
                       char *err, size_t err_size)
{
    *m = (struct peri_module){};
    if (len == 0 || len > max_module_file)
        return fail(err, err_size, "a module file must be 1 byte to 64 KiB");

    toml_result_t result = toml_parse(text, (int)len);
    bool ok;
    if (result.ok)
        ok = parse_fields(result.toptab, m, err, err_size);
    else
        ok = fail(err, err_size, "%s", result.errmsg);
    toml_free(result);

    if (!ok)
        *m = (struct peri_module){};
    return ok;
}

// The bytes of a regular file (no symlink as the last component), at most
// 64 KiB + 1 so the parser can reject oversize. Caller frees. Null with err set on failure.
static char *read_module_file(const char *path, size_t *len, char *err, size_t err_size)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        fail(err, err_size, "cannot open %s: %s", path, strerror(errno));
        return nullptr;
    }

    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        fail(err, err_size, "%s is not a regular file", path);
        return nullptr;
    }

    char *buf = malloc(max_module_file + 1);
    if (!buf) {
        close(fd);
        fail(err, err_size, "out of memory");
        return nullptr;
    }
    ssize_t n = read(fd, buf, max_module_file + 1);
    close(fd);
    if (n < 0) {
        free(buf);
        fail(err, err_size, "cannot read %s", path);
        return nullptr;
    }
    *len = (size_t)n;
    return buf;
}

bool peri_module_load(const char *path, struct peri_module *m, char *err, size_t err_size)
{
    *m = (struct peri_module){};
    size_t len;
    char *buf = read_module_file(path, &len, err, err_size);
    if (!buf)
        return false;
    bool ok = peri_module_parse(buf, len, m, err, err_size);
    free(buf);
    return ok;
}

bool peri_module_matches(const struct peri_module *m, const struct peri_device *d)
{
    return d->hidden == PERI_VISIBLE && m->vid == d->vid && m->pid == d->pid && m->has_usage
           && m->usage_page == d->usage_page && m->usage == d->usage;
}

enum peri_select peri_module_select(const struct peri_device *devs, size_t dev_count,
                                    const struct peri_module *installed, size_t installed_count,
                                    const char *id, char *node, size_t node_size,
                                    const struct peri_module **module)
{
    const struct peri_module *m = nullptr;
    for (size_t i = 0; i < installed_count && !m; i++)
        if (strcmp(installed[i].id, id) == 0)
            m = &installed[i];
    if (!m)
        return PERI_SELECT_NOT_INSTALLED;

    const struct peri_device *found = nullptr;
    for (size_t i = 0; i < dev_count; i++) {
        if (!peri_module_matches(m, &devs[i]))
            continue;
        if (found)
            return PERI_SELECT_AMBIGUOUS;
        found = &devs[i];
    }
    if (!found)
        return PERI_SELECT_NOT_CONNECTED;

    snprintf(node, node_size, "%s", found->node);
    if (module)
        *module = m;
    return PERI_SELECT_OK;
}

bool peri_module_install(const char *catalog_dir, const char *installed_dir, const char *id,
                         struct peri_module *out, char *err, size_t err_size)
{
    if (!peri_module_id_valid(id))
        return fail(err, err_size, "'%s' is not a valid module id", id);

    // Read once, validate those exact bytes, and write those same bytes.
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s/%s/module.toml", catalog_dir, id) >= (int)sizeof path)
        return fail(err, err_size, "catalog path too long");
    if (access(path, F_OK) < 0)
        return fail(err, err_size, "no module '%s' in the catalog", id);
    size_t len;
    char *buf = read_module_file(path, &len, err, err_size);
    if (!buf)
        return false;
    struct peri_module m;
    if (!peri_module_parse(buf, len, &m, err, err_size)) {
        free(buf);
        return false;
    }
    if (strcmp(m.id, id) != 0) {
        free(buf);
        return fail(err, err_size, "catalog entry '%s' has id '%s'", id, m.id);
    }

    int dirfd = -1, idfd = -1, tmpfd = -1;
    bool ok = false;
    static const char tmp_name[] = ".module.toml.tmp";
    if (!peri_mkdir_p(installed_dir)
        || (dirfd = open(installed_dir, O_DIRECTORY | O_CLOEXEC)) < 0) {
        fail(err, err_size, "cannot create %s: %s", installed_dir, strerror(errno));
    } else if ((mkdirat(dirfd, id, 0755) < 0 && errno != EEXIST)
               || (idfd = openat(dirfd, id, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) < 0) {
        fail(err, err_size, "cannot create %s/%s: %s", installed_dir, id, strerror(errno));
    } else if ((tmpfd = openat(idfd, tmp_name,
                               O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644)) < 0) {
        fail(err, err_size, "cannot write in %s/%s: %s", installed_dir, id, strerror(errno));
    } else {
        bool wrote = peri_write_all(tmpfd, buf, len);
        int saved = errno;
        bool closed = close(tmpfd) == 0; // always close, even after a failed write
        tmpfd = -1;
        if (!wrote || !closed) {
            unlinkat(idfd, tmp_name, 0);
            fail(err, err_size, "cannot write %s/%s: %s", installed_dir, id,
                 strerror(wrote ? errno : saved));
        } else if (renameat(idfd, tmp_name, idfd, "module.toml") < 0) {
            unlinkat(idfd, tmp_name, 0);
            fail(err, err_size, "cannot install %s: %s", id, strerror(errno));
        } else {
            ok = true;
            if (out)
                *out = m;
        }
    }
    if (tmpfd >= 0)
        close(tmpfd);
    if (idfd >= 0)
        close(idfd);
    if (dirfd >= 0)
        close(dirfd);
    free(buf);
    return ok;
}

// True if the directory holds nothing but module.toml.
static bool only_module_file(int idfd)
{
    int fd = dup(idfd);
    DIR *d = fd >= 0 ? fdopendir(fd) : nullptr;
    if (!d) {
        if (fd >= 0)
            close(fd);
        return false;
    }
    bool only = true;
    for (struct dirent *e; (e = readdir(d));) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0
            && strcmp(e->d_name, "module.toml") != 0)
            only = false;
    }
    closedir(d);
    return only;
}

bool peri_module_remove(const char *installed_dir, const char *id, char *err, size_t err_size)
{
    if (!peri_module_id_valid(id))
        return fail(err, err_size, "'%s' is not a valid module id", id);

    int dirfd = open(installed_dir, O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0)
        return fail(err, err_size, "module '%s' is not installed", id);
    // O_NOFOLLOW: an <id> that is a symlink is not ours to delete through.
    int idfd = openat(dirfd, id, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (idfd < 0) {
        close(dirfd);
        return fail(err, err_size, errno == ENOENT ? "module '%s' is not installed"
                                                   : "%s is not a plain directory",
                    id);
    }

    bool ok = false;
    if (!only_module_file(idfd))
        fail(err, err_size, "%s has files besides module.toml; not removing it", id);
    else if (unlinkat(idfd, "module.toml", 0) < 0)
        fail(err, err_size, "module '%s' is not installed", id);
    else if (unlinkat(dirfd, id, AT_REMOVEDIR) < 0)
        fail(err, err_size, "cannot remove %s: %s", id, strerror(errno));
    else
        ok = true;
    close(idfd);
    close(dirfd);
    return ok;
}

static int by_name(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

size_t peri_modules_scan(const char *dir, struct peri_module *out, size_t max)
{
    struct dirent **entries;
    int n = scandir(dir, &entries, nullptr, by_name);
    if (n < 0)
        return 0;

    size_t count = 0;
    for (int i = 0; i < n; i++) {
        const char *name = entries[i]->d_name;
        if (count < max && peri_module_id_valid(name)) {
            char path[PATH_MAX], err[200];
            snprintf(path, sizeof path, "%s/%s/module.toml", dir, name);
            if (!peri_module_load(path, &out[count], err, sizeof err))
                fprintf(stderr, "perid: skipping module %s: %s\n", name, err);
            else if (strcmp(out[count].id, name) != 0)
                fprintf(stderr, "perid: skipping module %s: its id is '%s'\n", name, out[count].id);
            else
                count++;
        }
        free(entries[i]);
    }
    free(entries);
    return count;
}

bool peri_installed_dir(char *buf, size_t size)
{
    const char *data = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    int n;
    if (data && *data)
        n = snprintf(buf, size, "%s/peri/modules", data);
    else if (home && *home)
        n = snprintf(buf, size, "%s/.local/share/peri/modules", home);
    else
        return false;
    return n > 0 && (size_t)n < size;
}

bool peri_catalog_dir(char *buf, size_t size)
{
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n < 0)
        return false;
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (!slash)
        return false;
    *slash = '\0';
    int len = snprintf(buf, size, "%s/../share/peri/catalog", exe);
    return len > 0 && (size_t)len < size;
}
