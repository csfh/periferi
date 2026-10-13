// peri: command line client for the peri core daemon.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "json.h"
#include "module.h"
#include "scan.h"
#include "udev.h"
#include "xap.h"

static int run(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

// Does the installed rule file hold exactly `want`? Any length: a longer file never matches
// by being cut off, so setup does not escalate on every run once the rules grow.
static bool rules_file_equals(const char *want)
{
    char *have = peri_udev_read_installed();
    bool same = have && strcmp(have, want) == 0;
    free(have);
    return same;
}

// Let the logged-in user open the hidraw nodes of the devices the installed modules drive,
// and nothing else. Asks for admin rights only if the rule file differs from what is needed.
static int setup_udev(void)
{
    char dir[PATH_MAX];
    struct peri_module mods[32];
    size_t count = peri_installed_dir(dir, sizeof dir) ? peri_modules_scan(dir, mods, 32) : 0;

    char rules[4096];
    size_t len = peri_udev_rules(mods, count, rules, sizeof rules);
    if (len == 0) {
        fprintf(stderr, "peri: too many modules for one udev rule file\n");
        return 1;
    }
    if (rules_file_equals(rules)) {
        printf("peri: udev rule already up to date (%zu module%s)\n", count, count == 1 ? "" : "s");
        return 0;
    }

    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime) {
        fprintf(stderr, "peri: XDG_RUNTIME_DIR is not set\n");
        return 1;
    }

    // Stage the rule as the user, then let root copy it into place.
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s/peri-rules-XXXXXX", runtime);
    int fd = mkstemp(tmp);
    if (fd < 0) {
        perror("peri: mkstemp");
        return 1;
    }
    bool ok = write(fd, rules, len) == (ssize_t)len;
    close(fd);

    if (ok) {
        printf("peri: installing %s for %zu module%s (asks for admin rights)\n",
               peri_udev_rules_path, count, count == 1 ? "" : "s");
        char *const argv[] = {
            "pkexec", "sh", "-c",
            "install -m644 \"$1\" \"$2\""
            " && udevadm control --reload"
            " && udevadm trigger --subsystem-match=hidraw --action=change",
            "sh", tmp, (char *)peri_udev_rules_path, nullptr,
        };
        ok = run(argv) == 0;
    }
    unlink(tmp);

    if (!ok)
        fprintf(stderr, "peri: installing the udev rule failed\n");
    return ok ? 0 : 1;
}

// mkdir -p, mode 0755. Returns 0, or -1 with errno set.
static int mkdir_p(const char *dir)
{
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s", dir) >= (int)sizeof path) {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (char *p = path + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        int r = mkdir(path, 0755);
        *p = '/';
        if (r < 0 && errno != EEXIST)
            return -1;
    }
    return mkdir(path, 0755) < 0 && errno != EEXIST ? -1 : 0;
}

static bool daemon_accepts(void)
{
    struct sockaddr_un addr;
    if (peri_socket_addr(&addr) < 0)
        return false;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    bool up = connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0;
    close(fd);
    return up;
}

// Install perid as a systemd *user* service. No root involved.
static int setup_service(void)
{
    // The unit runs the perid that sits next to this peri binary.
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n < 0) {
        perror("peri: readlink");
        return 1;
    }
    self[n] = '\0';
    *strrchr(self, '/') = '\0';

    char perid[PATH_MAX + 8];
    snprintf(perid, sizeof perid, "%s/perid", self);
    if (access(perid, X_OK) < 0 || strpbrk(perid, "%\"\\")) {
        fprintf(stderr, "peri: cannot use %s as the service binary\n", perid);
        return 1;
    }

    const char *config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char dir[PATH_MAX];
    if (config && *config)
        snprintf(dir, sizeof dir, "%s/systemd/user", config);
    else if (home && *home)
        snprintf(dir, sizeof dir, "%s/.config/systemd/user", home);
    else {
        fprintf(stderr, "peri: neither XDG_CONFIG_HOME nor HOME is set\n");
        return 1;
    }
    if (mkdir_p(dir) < 0) {
        perror("peri: mkdir");
        return 1;
    }

    char unit_path[PATH_MAX + 16];
    snprintf(unit_path, sizeof unit_path, "%s/perid.service", dir);
    FILE *f = fopen(unit_path, "w");
    if (!f) {
        perror("peri: perid.service");
        return 1;
    }
    fprintf(f,
            "[Unit]\n"
            "Description=peri peripheral daemon\n"
            "\n"
            "[Service]\n"
            "ExecStart=\"%s\"\n"
            "Restart=on-failure\n"
            "\n"
            "[Install]\n"
            "WantedBy=default.target\n",
            perid);
    if (fclose(f) != 0) {
        perror("peri: perid.service");
        return 1;
    }

    char *const reload[] = { "systemctl", "--user", "daemon-reload", nullptr };
    char *const enable[] = { "systemctl", "--user", "enable", "perid.service", nullptr };
    char *const restart[] = { "systemctl", "--user", "restart", "perid.service", nullptr };
    if (run(reload) < 0 || run(enable) < 0 || run(restart) < 0) {
        fprintf(stderr, "peri: systemctl failed\n");
        return 1;
    }
    // systemctl returns once perid is spawned, not once it listens. Wait for it.
    for (int i = 0; i < 20 && !daemon_accepts(); i++)
        usleep(100000);
    if (!daemon_accepts()) {
        fprintf(stderr, "peri: perid did not come up; see `journalctl --user -u perid`\n");
        return 1;
    }
    printf("peri: perid service enabled and running (%s)\n", unit_path);
    return 0;
}

static ssize_t request(const char *req, char *buf, size_t size);

// Send "use <id>" or "remove <id>" to the daemon and print its one-line answer.
static int cmd_module_verb(const char *verb, const char *id)
{
    char req[96], reply[256];
    if (snprintf(req, sizeof req, "%s %s\n", verb, id) >= (int)sizeof req) {
        fprintf(stderr, "peri: module id too long\n");
        return 2;
    }
    if (request(req, reply, sizeof reply) <= 0)
        return 1;
    if (strncmp(reply, "ok ", 3) != 0) {
        fputs(strncmp(reply, "err ", 4) == 0 ? reply + 4 : reply, stderr);
        return 1;
    }
    fputs(reply + 3, stdout);
    return 0;
}

// The CTRL module used to be implicit. The first time setup runs with no modules directory,
// install it so an existing setup keeps working. After that the directory exists, and
// `peri remove` stays removed. Done here, not through the daemon, so the check and the
// install see the same XDG_DATA_HOME.
static int setup_default_modules(void)
{
    char dir[PATH_MAX], catalog[PATH_MAX], err[200];
    if (!peri_installed_dir(dir, sizeof dir)) {
        fprintf(stderr, "peri: neither XDG_DATA_HOME nor HOME is set\n");
        return 1;
    }
    if (access(dir, F_OK) == 0)
        return 0;
    struct peri_module m;
    if (!peri_catalog_dir(catalog, sizeof catalog)
        || !peri_module_install(catalog, dir, "drop-ctrl-v2", &m, err, sizeof err)) {
        fprintf(stderr, "peri: installing the default module failed: %s\n", err);
        return 1;
    }
    printf("peri: installed module %s %s\n", m.id, m.version);
    return 0;
}

// peri setup [--no-udev]. --no-udev skips the step that needs admin rights.
static int cmd_setup(bool with_udev)
{
    if (setup_default_modules() != 0 || (with_udev && setup_udev() != 0) || setup_service() != 0) {
        fprintf(stderr, "peri: setup failed\n");
        return 1;
    }
    printf("peri: setup complete\n");
    return 0;
}

// Send one request line to perid and read its whole reply (until it closes
// the connection). Returns the reply length, or -1 after printing an error.
static ssize_t request(const char *req, char *buf, size_t size)
{
    struct sockaddr_un addr;
    if (peri_socket_addr(&addr) < 0) {
        fprintf(stderr, "peri: XDG_RUNTIME_DIR is not set or too long\n");
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("peri: socket");
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        fprintf(stderr, "peri: cannot reach perid (%s)\n", strerror(errno));
        close(fd);
        return -1;
    }

    ssize_t total = -1;
    size_t req_len = strlen(req);
    if (write(fd, req, req_len) == (ssize_t)req_len) {
        total = 0;
        ssize_t n;
        while ((size_t)total < size - 1 && (n = read(fd, buf + total, size - 1 - (size_t)total)) > 0)
            total += n;
    }
    close(fd);

    if (total < 0) {
        fprintf(stderr, "peri: request to perid failed\n");
        return -1;
    }
    buf[total] = '\0';
    return total;
}

static int cmd_ping(void)
{
    char reply[64];
    if (request("ping\n", reply, sizeof reply) <= 0)
        return 1;
    fputs(reply, stdout);
    return 0;
}

struct row { // one HID interface, as reported by perid
    char node[32], hidden[8], group[64], name[96];
    unsigned vid, pid, page, usage;
};

// peri list --all (text): one line per HID interface, hidden ones included and labelled.
// These are the only lines that show hidraw node numbers.
static int list_interfaces(void)
{
    char reply[8192]; // as large as the daemon's list buffer
    ssize_t n = request("list\n", reply, sizeof reply);
    if (n < 0)
        return 1;

    struct row rows[32];
    size_t count = 0;
    for (char *line = strtok(reply, "\n"); line && count < 32; line = strtok(nullptr, "\n")) {
        struct row *r = &rows[count];
        if (sscanf(line, "%31s %x:%x %x:%x %7s %63s %95[^\n]", r->node, &r->vid, &r->pid,
                   &r->page, &r->usage, r->hidden, r->group, r->name) == 8)
            count++;
    }
    if (count == 0) {
        puts("no HID devices found");
        return 0;
    }

    printf("%-9s %-11s%-11s%-8s%s\n", "NODE", "ID", "USAGE", "HIDDEN", "NAME");
    for (size_t i = 0; i < count; i++)
        printf("%-9s %04x:%04x  %04x:%04x  %-7s %s\n", rows[i].node, rows[i].vid, rows[i].pid,
               rows[i].page, rows[i].usage, strcmp(rows[i].hidden, "-") ? rows[i].hidden : "",
               rows[i].name);
    return 0;
}

struct dev_row { // one physical device, as reported by perid's "devices"
    char status[16], module[49], hidden[8], group[64], name[96];
    unsigned vid, pid;
};

static bool is_hidden(const struct dev_row *r)
{
    return strcmp(r->hidden, "-") != 0;
}

// {"group":..., "vid":"359b", "pid":"0009", "name":..., "status":"installed|available|none",
//  "module":"<id>"|null, "hidden":"fido"|"camera"|null}
static void print_json(const struct dev_row *rows, size_t count, bool all)
{
    fputc('[', stdout);
    bool first = true;
    for (size_t i = 0; i < count; i++) {
        const struct dev_row *r = &rows[i];
        if (is_hidden(r) && !all)
            continue;
        fputs(first ? "\n  " : ",\n  ", stdout);
        first = false;
        fputs("{\"group\": ", stdout);
        peri_json_string(stdout, r->group);
        printf(", \"vid\": \"%04x\", \"pid\": \"%04x\", \"name\": ", r->vid, r->pid);
        peri_json_string(stdout, r->name);
        printf(", \"status\": \"%s\", \"module\": ", r->status);
        if (strcmp(r->module, "-") == 0)
            fputs("null", stdout);
        else
            peri_json_string(stdout, r->module);
        fputs(", \"hidden\": ", stdout);
        if (is_hidden(r))
            peri_json_string(stdout, r->hidden);
        else
            fputs("null", stdout);
        fputc('}', stdout);
    }
    puts(first ? "]" : "\n]");
}

static void print_table(const struct dev_row *rows, size_t count)
{
    printf("%-11s%-32s%-11s%s\n", "ID", "NAME", "STATUS", "MODULE");
    size_t hidden = 0;
    for (size_t i = 0; i < count; i++) {
        const struct dev_row *r = &rows[i];
        if (is_hidden(r)) {
            hidden++;
            continue;
        }
        printf("%04x:%04x  %-30s  %-11s%s\n", r->vid, r->pid, r->name, r->status, r->module);
    }
    if (hidden)
        printf("(%zu hidden: security keys and cameras; `peri list --all` shows them)\n", hidden);
}

// peri list [--all] [--json]: physical devices with their module status. --all shows
// nothing hidden: hidden devices in the table and JSON, every interface in the text view.
static int cmd_list(int argc, char **argv)
{
    bool all = false, json = false;
    for (int i = 2; i < argc; i++) {
        bool *flag = strcmp(argv[i], "--all") == 0    ? &all
                     : strcmp(argv[i], "--json") == 0 ? &json
                                                      : nullptr;
        if (!flag || *flag) {
            fprintf(stderr, "usage: peri list [--all] [--json]\n");
            return 2;
        }
        *flag = true;
    }
    if (all && !json)
        return list_interfaces();

    char reply[8192];
    if (request("devices\n", reply, sizeof reply) < 0)
        return 1; // nothing on stdout: a JSON consumer must not see a half answer

    struct dev_row rows[32];
    size_t count = 0;
    for (char *line = strtok(reply, "\n"); line && count < 32; line = strtok(nullptr, "\n")) {
        struct dev_row *r = &rows[count];
        if (sscanf(line, "%x:%x %15s %48s %7s %63s %95[^\n]", &r->vid, &r->pid, r->status,
                   r->module, r->hidden, r->group, r->name) == 7)
            count++;
    }

    if (json)
        print_json(rows, count, all);
    else if (count == 0)
        puts("no HID devices found");
    else
        print_table(rows, count);
    return 0;
}

// Accept "hidraw7" or "/dev/hidraw7", nothing else. Returns the bare name, or
// nullptr after printing an error.
static const char *node_name(const char *arg)
{
    const char *name = strncmp(arg, "/dev/", 5) == 0 ? arg + 5 : arg;
    if (strncmp(name, "hidraw", 6) != 0 || !name[6]
        || strspn(name + 6, "0123456789") != strlen(name + 6)) {
        fprintf(stderr, "peri: expected a node like hidraw7 (see `peri list --all`)\n");
        return nullptr;
    }
    return name;
}

// `monitor` and `xap` open the node they are given, so they check first what it
// is, from sysfs. A security key (any interface of its USB device) is never
// opened. `xap` writes, so it also needs the node to be an XAP control interface.
static bool node_allowed(const char *name, bool needs_xap)
{
    struct peri_device devs[128];
    size_t count = peri_scan(devs, 128);
    switch (peri_node_check(devs, count, name, needs_xap)) {
    case PERI_NODE_OK:
        return true;
    case PERI_NODE_MISSING:
        fprintf(stderr, "peri: %s: no such HID device (see `peri list --all`)\n", name);
        break;
    case PERI_NODE_SECURITY_KEY:
        fprintf(stderr, "peri: %s belongs to a security key; refusing to open it\n", name);
        break;
    case PERI_NODE_NOT_XAP:
        fprintf(stderr, "peri: %s is not an XAP control interface (ff51:0058); refusing\n", name);
        break;
    }
    return false;
}

static volatile sig_atomic_t interrupted;

static void on_interrupt(int)
{
    interrupted = 1;
}

// Print every input report of a hidraw node as "<seconds> <hex bytes>" until
// interrupted. Opens the node read-only, so nothing is ever sent to the device.
static int cmd_monitor(const char *arg)
{
    const char *name = node_name(arg);
    if (!name)
        return 2;
    if (!node_allowed(name, false))
        return 1;

    char path[64];
    snprintf(path, sizeof path, "/dev/%s", name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "peri: %s: %s%s\n", path, strerror(errno),
                errno == EACCES ? " (try `peri setup`)" : "");
        return 1;
    }

    // No SA_RESTART: Ctrl-C must interrupt the blocking read().
    struct sigaction sa = { .sa_handler = on_interrupt };
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    setvbuf(stdout, nullptr, _IOLBF, 0); // one report per line, even into a pipe

    printf("# %s\n", name);
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    uint8_t report[4096];
    while (!interrupted) {
        ssize_t n = read(fd, report, sizeof report);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "peri: read %s: %s\n", path, strerror(errno));
            close(fd);
            return 1;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long sec = now.tv_sec - start.tv_sec;
        long usec = (now.tv_nsec - start.tv_nsec) / 1000;
        if (usec < 0) {
            sec--;
            usec += 1000000;
        }

        printf("%ld.%06ld", sec, usec);
        for (ssize_t i = 0; i < n; i++)
            printf(" %02x", report[i]);
        putchar('\n');
    }
    close(fd);
    return 0;
}

// Read-only XAP queries that take no request payload. This is an allowlist on
// purpose: secure routes (bootloader jump, EEPROM reset, remapping) are never
// on it, so `peri xap` cannot send them.
static const struct { uint8_t len; uint8_t route[3]; } xap_queries[] = {
    { 2, { 0x00, 0x00 } }, // XAP version
    { 2, { 0x00, 0x01 } }, // XAP capabilities
    { 2, { 0x00, 0x02 } }, // enabled subsystems
    { 2, { 0x00, 0x03 } }, // secure status
    { 2, { 0x01, 0x00 } }, // QMK version
    { 2, { 0x01, 0x01 } }, // QMK capabilities
    { 2, { 0x01, 0x02 } }, // board identifiers (VID, PID, version, QMK id)
    { 2, { 0x01, 0x03 } }, // manufacturer
    { 2, { 0x01, 0x04 } }, // product name
    { 2, { 0x01, 0x05 } }, // config blob length
    { 2, { 0x01, 0x08 } }, // hardware identifier
    { 2, { 0x01, 0x10 } }, // keycodes version
    { 2, { 0x04, 0x01 } }, // keymap capabilities
    { 2, { 0x04, 0x02 } }, // keymap layer count
    { 2, { 0x06, 0x01 } }, // lighting capabilities
    { 3, { 0x06, 0x04, 0x01 } }, // rgbmatrix capabilities
    { 3, { 0x06, 0x04, 0x02 } }, // rgbmatrix enabled effects
    { 3, { 0x06, 0x04, 0x03 } }, // rgbmatrix current config
};

static bool xap_query_allowed(const uint8_t *route, size_t len)
{
    for (size_t i = 0; i < sizeof xap_queries / sizeof *xap_queries; i++)
        if (xap_queries[i].len == len && memcmp(xap_queries[i].route, route, len) == 0)
            return true;
    return false;
}

// Send one XAP query to a device's vendor interface and print the reply as
// "<ok|failed|secure-locked> <length> <payload hex>".
static int cmd_xap(const char *arg, const char *route_hex)
{
    const char *name = node_name(arg);
    if (!name)
        return 2;

    uint8_t route[3];
    ssize_t route_len = peri_parse_hex(route_hex, route, sizeof route);
    if (route_len < 0) {
        fprintf(stderr, "peri: route must be 2-3 bytes of hex, like 0000\n");
        return 2;
    }
    if (!xap_query_allowed(route, (size_t)route_len)) {
        fprintf(stderr, "peri: route %s is not a known read-only query; refusing\n", route_hex);
        return 2;
    }
    if (!node_allowed(name, true))
        return 1;

    int fd = xap_open(name);
    if (fd < 0)
        return 1;
    struct xap_reply reply;
    int rc = xap_call(fd, route, (size_t)route_len, nullptr, 0, &reply);
    close(fd);
    if (rc < 0)
        return 1;

    printf("%s %zu", reply.flags & 0x01 ? "ok" : reply.flags & 0x02 ? "secure-locked" : "failed",
           reply.len);
    for (size_t i = 0; i < reply.len; i++)
        printf(" %02x", reply.payload[i]);
    putchar('\n');
    return reply.flags & 0x01 ? 0 : 1;
}

struct mod_row { // one module, as reported by perid's "modules"
    char id[49], status[16], access[8], version[33], name[65];
    unsigned vid, pid;
};

// [{"id":..., "name":..., "version":..., "vid":"359b", "pid":"0009",
//   "status":"installed|available", "access":"ok|setup"|null}]
// access: "setup" means `peri setup` has to update the udev rule for this module's device.
static void print_modules_json(const struct mod_row *rows, size_t count)
{
    fputc('[', stdout);
    for (size_t i = 0; i < count; i++) {
        const struct mod_row *r = &rows[i];
        fputs(i ? ",\n  " : "\n  ", stdout);
        fputs("{\"id\": ", stdout);
        peri_json_string(stdout, r->id);
        fputs(", \"name\": ", stdout);
        peri_json_string(stdout, r->name);
        fputs(", \"version\": ", stdout);
        peri_json_string(stdout, r->version);
        printf(", \"vid\": \"%04x\", \"pid\": \"%04x\", \"status\": \"%s\", \"access\": ", r->vid,
               r->pid, r->status);
        if (strcmp(r->access, "-") == 0)
            fputs("null", stdout);
        else
            peri_json_string(stdout, r->access);
        fputc('}', stdout);
    }
    puts(count ? "\n]" : "]");
}

// peri modules [--json]: the bundled catalog plus whatever is installed.
static int cmd_modules(int argc, char **argv)
{
    bool json = false;
    if (argc == 3 && strcmp(argv[2], "--json") == 0) {
        json = true;
    } else if (argc != 2) {
        fprintf(stderr, "usage: peri modules [--json]\n");
        return 2;
    }

    char reply[4096];
    if (request("modules\n", reply, sizeof reply) < 0)
        return 1; // nothing on stdout: a JSON consumer must not see a half answer

    struct mod_row rows[32];
    size_t count = 0;
    for (char *line = strtok(reply, "\n"); line && count < 32; line = strtok(nullptr, "\n")) {
        struct mod_row *r = &rows[count];
        if (sscanf(line, "%48s %15s %7s %x:%x %32s %64[^\n]", r->id, r->status, r->access, &r->vid,
                   &r->pid, r->version, r->name) == 7)
            count++;
    }

    if (json) {
        print_modules_json(rows, count);
    } else if (count == 0) {
        puts("no modules");
    } else {
        printf("%-16s %-10s %-8s %-10s %-8s %s\n", "ID", "STATUS", "ACCESS", "DEVICE", "VERSION",
               "NAME");
        for (size_t i = 0; i < count; i++)
            printf("%-16s %-10s %-8s %04x:%04x  %-8s %s\n", rows[i].id, rows[i].status,
                   strcmp(rows[i].access, "-") ? rows[i].access : "", rows[i].vid, rows[i].pid,
                   rows[i].version, rows[i].name);
    }
    return 0;
}

// peri rgb get
// peri rgb set <key=value>...
// The daemon owns the keyboard: this only forwards the request and prints the reply.
static int cmd_rgb(int argc, char **argv)
{
    char req[256] = "rgb";
    size_t len = strlen(req);
    for (int i = 2; i < argc; i++) {
        int n = snprintf(req + len, sizeof req - len, " %s", argv[i]);
        if (n < 0 || (size_t)n >= sizeof req - len - 2) { // keep room for "\n" and NUL
            fprintf(stderr, "peri: request too long\n");
            return 2;
        }
        len += (size_t)n;
    }
    req[len] = '\n';
    req[len + 1] = '\0';

    char reply[512];
    if (request(req, reply, sizeof reply) <= 0)
        return 1;
    if (strncmp(reply, "ok", 2) != 0) {
        fputs(strncmp(reply, "err ", 4) == 0 ? reply + 4 : reply, stderr);
        return 1;
    }

    // "ok key=value key=value ...": print as a table, whatever the keys are.
    for (char *tok = strtok(reply + 2, " \n"); tok; tok = strtok(nullptr, " \n")) {
        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            printf("%-8s %s\n", tok, eq + 1);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "setup") == 0)
        return cmd_setup(true);
    if (argc == 3 && strcmp(argv[1], "setup") == 0 && strcmp(argv[2], "--no-udev") == 0)
        return cmd_setup(false);
    if (argc == 2 && strcmp(argv[1], "ping") == 0)
        return cmd_ping();
    if (argc >= 2 && strcmp(argv[1], "list") == 0)
        return cmd_list(argc, argv);

    if (argc == 3 && strcmp(argv[1], "monitor") == 0)
        return cmd_monitor(argv[2]);

    if (argc >= 2 && strcmp(argv[1], "modules") == 0)
        return cmd_modules(argc, argv);
    if (argc == 3 && strcmp(argv[1], "use") == 0)
        return cmd_module_verb("use", argv[2]);
    if (argc == 3 && strcmp(argv[1], "remove") == 0)
        return cmd_module_verb("remove", argv[2]);
    if (argc == 4 && strcmp(argv[1], "xap") == 0)
        return cmd_xap(argv[2], argv[3]);
    if (argc >= 3 && strcmp(argv[1], "rgb") == 0)
        return cmd_rgb(argc, argv);

    fprintf(stderr, "usage: peri <setup [--no-udev]|ping|list [--all] [--json]|modules [--json]|use <id>|remove <id>|monitor <node>|xap <node> <route-hex>|rgb get|set <key=value>...|save>\n");
    return 2;
}
