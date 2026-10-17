// peri: command line client for the peri core daemon.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/hidraw.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "fsutil.h"
#include "json.h"
#include "module.h"
#include "razer90_dev.h"
#include "razer_dev.h"
#include "razer_scan.h"
#include "request.h"
#include "scan.h"
#include "service.h"
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

    // Stage the rule as the user, then let root copy it into place. A path that
    // does not fit is refused rather than written truncated.
    char tmp[256];
    if (!peri_xdg_path(tmp, sizeof tmp, PERI_XDG_RUNTIME, "peri-rules-XXXXXX")) {
        fprintf(stderr, "peri: XDG_RUNTIME_DIR is not set\n");
        return 1;
    }
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
    if (access(perid, X_OK) < 0) {
        fprintf(stderr, "peri: cannot use %s as the service binary\n", perid);
        return 1;
    }

    // Same bases the modules and saved looks were just checked against. They
    // are pinned into the unit below: a user service does not inherit this
    // shell's XDG variables, so the daemon would otherwise look elsewhere.
    char config_home[PATH_MAX], data_home[PATH_MAX];
    if (!peri_xdg_path(config_home, sizeof config_home, PERI_XDG_CONFIG, nullptr)
        || !peri_xdg_path(data_home, sizeof data_home, PERI_XDG_DATA, nullptr)) {
        fprintf(stderr, "peri: neither XDG_CONFIG_HOME/XDG_DATA_HOME nor HOME is set\n");
        return 1;
    }
    // Three absolute paths, each up to PATH_MAX, plus the unit text around them.
    char unit[3 * PATH_MAX + 512];
    if (!peri_user_unit(unit, sizeof unit, perid, config_home, data_home)) {
        fprintf(stderr, "peri: cannot pin the perid path or the config and data directories into the service\n");
        return 1;
    }

    // Same config base as saved looks. A path that does not fit is refused
    // rather than creating a truncated directory.
    char dir[PATH_MAX];
    if (!peri_xdg_path(dir, sizeof dir, PERI_XDG_CONFIG, "systemd/user")) {
        fprintf(stderr, "peri: neither XDG_CONFIG_HOME nor HOME is set\n");
        return 1;
    }
    if (!peri_mkdir_p(dir)) {
        perror("peri: mkdir");
        return 1;
    }

    char unit_path[PATH_MAX + 16];
    snprintf(unit_path, sizeof unit_path, "%s/perid.service", dir);
    // Replaced whole or not at all: a half-written unit would pin nothing, or
    // the wrong path, and must not destroy the working one.
    if (!peri_write_file_atomic(unit_path, unit, strlen(unit))) {
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
    // Shown because a shell with only some throwaway XDG variables would pin
    // the wrong directories into the real unit: the owner can see it at once.
    printf("peri: perid service enabled and running (%s)\n"
           "peri: pinned XDG_CONFIG_HOME=%s XDG_DATA_HOME=%s\n",
           unit_path, config_home, data_home);
    return 0;
}

static ssize_t request(const char *req, char *buf, size_t size);

// Send "use <id>" or "remove <id>" to the daemon and print its one-line answer.
static int cmd_module_verb(const char *verb, const char *id)
{
    char *const words[] = { (char *)id };
    char req[PERI_REQUEST_MAX + 1], reply[256];
    if (!peri_request_line(req, sizeof req, verb, words, 1)) {
        fprintf(stderr, peri_request_word_ok(id) ? "peri: module id too long\n"
                                                 : "peri: a module id cannot be empty or contain a space\n");
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
//  "module":"<id>"|null, "hidden":"fido"|null}
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
        printf("(%zu hidden: security keys; `peri list --all` shows them)\n", hidden);
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

// Read one feature report of a hidraw node and print it as hex. A GET_FEATURE
// only asks the device for a report; it never sends one. Used to look at vendor
// interfaces nobody has documented yet. The kernel passes the buffer size on as
// the request length, so `len` (default 64, id byte included) matters to the device.
static int cmd_feature(const char *arg, const char *id_arg, const char *len_arg)
{
    const char *name = node_name(arg);
    if (!name)
        return 2;
    char *end;
    long id = strtol(id_arg, &end, 16);
    if (*id_arg == '\0' || *end != '\0' || id < 0 || id > 0xFF) {
        fprintf(stderr, "peri: report id must be 1 byte of hex, like 07\n");
        return 2;
    }
    long len = 64;
    if (len_arg) {
        len = strtol(len_arg, &end, 10);
        if (*len_arg == '\0' || *end != '\0' || len < 2 || len > 256) {
            fprintf(stderr, "peri: length must be 2-256 bytes\n");
            return 2;
        }
    }
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

    uint8_t buf[256] = { (uint8_t)id };
    int n = ioctl(fd, HIDIOCGFEATURE(len), buf);
    int err = errno;
    close(fd);
    if (n < 0) {
        fprintf(stderr, "peri: get feature %02lx on %s: %s\n", id, name, strerror(err));
        return 1;
    }

    printf("%d", n);
    for (int i = 0; i < n; i++)
        printf(" %02x", buf[i]);
    putchar('\n');
    return 0;
}

// Razer queries `peri razer` may send. An allowlist like the XAP one: it holds only queries
// that read (firmware get, brightness get, and the standard-LED getters for effect and colour).
// Setting a device mode or any lighting is never on it. `args` is at least `size` long. The
// getters take variable storage 1 and LED id 0, then zero placeholders for the values.
static const struct { const char *name; uint8_t cls, cmd, size; uint8_t args[5]; } razer_queries[] = {
    { "firmware", 0x00, 0x81, 2, { 0, 0, 0, 0, 0 } },
    { "brightness", 0x0f, 0x84, 3, { 0x01, 0x00, 0x00, 0, 0 } },
    { "led-effect", 0x03, 0x82, 3, { 0x01, 0x00, 0x00, 0, 0 } },
    { "led-rgb", 0x03, 0x81, 5, { 0x01, 0x00, 0x00, 0x00, 0x00 } },
};

// Send one allowlisted query to a Razer control interface and print the raw reply frame,
// then what it decodes to. Anything that is not a clean reply is reported, never retried
// by other means.
static int cmd_razer(const char *arg, const char *query)
{
    const char *name = node_name(arg);
    if (!name)
        return 2;

    size_t q = 0;
    while (q < sizeof razer_queries / sizeof *razer_queries && strcmp(razer_queries[q].name, query))
        q++;
    if (q == sizeof razer_queries / sizeof *razer_queries) {
        fprintf(stderr, "peri: unknown razer query '%s' (known: firmware, brightness, led-effect, led-rgb)\n", query);
        return 2;
    }
    if (!node_allowed(name, false))
        return 1;

    int fd = razer_open(name);
    if (fd < 0)
        return 1;
    struct razer_reply reply;
    uint8_t raw[RAZER_REPORT_LEN];
    int got;
    int rc = razer_call(fd, razer_queries[q].cls, razer_queries[q].cmd, razer_queries[q].size,
                        razer_queries[q].args, &reply, raw, &got);
    close(fd);

    if (got > 0) {
        printf("raw %d bytes:", got);
        for (int i = 0; i < got && i < RAZER_REPORT_LEN; i++)
            printf(" %02x", raw[i]);
        putchar('\n');
    } else {
        printf("raw none (last get feature returned %d bytes)\n", got);
    }
    if (rc < 0) {
        printf("failed %d\n", rc);
        return 1;
    }
    printf("status %02x %zu", reply.status, reply.len);
    for (size_t i = 0; i < reply.len; i++)
        printf(" %02x", reply.args[i]);
    putchar('\n');
    return reply.status == RAZER_STATUS_OK ? 0 : 1;
}

// The queries `peri razer90` may send to the DeathAdder: reads only (firmware, battery, charging,
// DPI, poll rate). Nothing that sets a mode, a DPI, a poll rate or lighting is on it. `args` is
// at least `size` long. Prints the raw bytes first, so a reply that does not decode can be told
// from a wrong frame.
static const struct { const char *name; uint8_t cls, cmd, size; uint8_t args[7]; } razer90_queries[] = {
    { "firmware", 0x00, 0x81, 2, { 0 } },
    { "battery", 0x07, 0x80, 2, { 0 } },
    { "charging", 0x07, 0x84, 2, { 0 } },
    { "dpi", 0x04, 0x85, 7, { 0 } },
    { "poll-rate", 0x00, 0x85, 1, { 0 } },
};

static void print_razer90_meaning(const char *query, const struct razer90_reply *reply)
{
    uint8_t level;
    bool charging;
    struct razer90_dpi dpi;
    unsigned hz;
    if (!strcmp(query, "battery") && razer90_battery_decode(reply, &level))
        printf("battery raw %u (%u%% if the byte spans 0-255, unproven)\n", level, razer90_battery_percent(level));
    else if (!strcmp(query, "charging") && razer90_charging_decode(reply, &charging))
        printf("charging %s\n", charging ? "yes" : "no");
    else if (!strcmp(query, "dpi") && razer90_dpi_decode(reply, &dpi))
        printf("dpi %u:%u\n", dpi.x, dpi.y);
    else if (!strcmp(query, "poll-rate") && razer90_poll_decode(reply, &hz)) {
        if (hz)
            printf("poll-rate %u Hz\n", hz);
        else // a code this program does not know (the dongle's faster rates use other codes)
            printf("poll-rate unknown code 0x%02x\n", reply->args[0]);
    }
}

static int cmd_razer90(const char *arg, const char *query)
{
    const char *name = node_name(arg);
    if (!name)
        return 2;
    size_t q = 0;
    while (q < sizeof razer90_queries / sizeof *razer90_queries && strcmp(razer90_queries[q].name, query))
        q++;
    if (q == sizeof razer90_queries / sizeof *razer90_queries) {
        fprintf(stderr, "peri: unknown razer90 query '%s' (known: firmware, battery, charging, dpi, poll-rate)\n",
                query);
        return 2;
    }
    if (!node_allowed(name, false))
        return 1;

    int fd = razer90_open(name);
    if (fd < 0)
        return 1;
    struct razer90_reply reply;
    uint8_t raw[RAZER90_HIDRAW_LEN];
    int got;
    int rc = razer90_call(fd, razer90_queries[q].cls, razer90_queries[q].cmd, razer90_queries[q].size,
                          razer90_queries[q].args, &reply, raw, &got);
    close(fd);

    if (got > 0) {
        printf("raw %d bytes:", got);
        for (int i = 0; i < got && i < RAZER90_HIDRAW_LEN; i++)
            printf(" %02x", raw[i]);
        putchar('\n');
    } else {
        printf("raw none (last get feature returned %d bytes)\n", got);
    }
    if (rc < 0) {
        printf("failed %d\n", rc);
        return 1;
    }
    printf("status %02x %zu", reply.status, reply.len);
    for (size_t i = 0; i < reply.len; i++)
        printf(" %02x", reply.args[i]);
    putchar('\n');
    if (reply.status == RAZER_STATUS_OK)
        print_razer90_meaning(query, &reply);
    return reply.status == RAZER_STATUS_OK ? 0 : 1;
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
    char id[49], status[16], access[8], engine[16], version[33], name[65];
    unsigned vid, pid;
};

// [{"id":..., "name":..., "version":..., "engine":"xap|razer|razer90|v4l2", "vid":"359b", "pid":"0009",
//   "status":"installed|available", "access":"ok|setup"|null}]
// access: "setup" means `peri setup` has to update the udev rule for this module's device.
// engine: which built-in protocol drives the device, so a UI can tell a keyboard from a microphone.
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
        fputs(", \"engine\": ", stdout);
        peri_json_string(stdout, r->engine);
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
// Developer probe: the keyboard's configuration blob (routes 0x01 0x05 length, 0x01 0x06 chunk),
// written raw to stdout. Read-only; the node must be an XAP control interface. Chunks are 32 bytes
// at a u16 little-endian offset (xap_0.3.1). The blob is the board's own description of itself.
static int cmd_blob(const char *arg)
{
    const char *name = node_name(arg);
    if (!name)
        return 2;
    if (!node_allowed(name, true))
        return 1;
    if (isatty(STDOUT_FILENO)) {
        fprintf(stderr, "peri: the blob is binary; redirect it to a file\n");
        return 2;
    }

    int fd = xap_open(name);
    if (fd < 0)
        return 1;

    static const uint8_t length_route[] = { 0x01, 0x05 };
    static const uint8_t chunk_route[] = { 0x01, 0x06 };
    struct xap_reply reply;
    if (xap_call(fd, length_route, sizeof length_route, nullptr, 0, &reply) < 0
        || !(reply.flags & 0x01) || reply.len != 2) {
        fprintf(stderr, "peri: could not read the blob length\n");
        close(fd);
        return 1;
    }
    size_t total = reply.payload[0] | (size_t)reply.payload[1] << 8;
    if (total == 0 || total > 16384) { // far above the 1615 bytes seen; refuse nonsense
        fprintf(stderr, "peri: implausible blob length %zu\n", total);
        close(fd);
        return 1;
    }

    for (size_t offset = 0; offset < total;) {
        uint8_t payload[2] = { (uint8_t)offset, (uint8_t)(offset >> 8) };
        if (xap_call(fd, chunk_route, sizeof chunk_route, payload, sizeof payload, &reply) < 0
            || !(reply.flags & 0x01) || reply.len == 0) {
            fprintf(stderr, "peri: reading the blob failed at offset %zu\n", offset);
            close(fd);
            return 1;
        }
        size_t take = reply.len < total - offset ? reply.len : total - offset;
        if (fwrite(reply.payload, 1, take, stdout) != take) {
            perror("peri: write");
            close(fd);
            return 1;
        }
        offset += take;
    }
    close(fd);
    fprintf(stderr, "peri: read %zu bytes\n", total);
    return 0;
}

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
        if (sscanf(line, "%48s %15s %7s %15s %x:%x %32s %64[^\n]", r->id, r->status, r->access,
                   r->engine, &r->vid, &r->pid, r->version, r->name) == 8)
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

// Send one request line to the daemon and print an "ok key=value ..." reply as a table, whatever
// the keys are. An "err ..." reply goes to stderr.
static int forward(const char *req)
{
    char reply[1024]; // the effect list alone is about 700 bytes
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

// One verb plus the caller's words, capped at what perid will read whole.
static int forward_words(const char *verb, char *const *words, int nwords)
{
    char req[PERI_REQUEST_MAX + 1];
    if (!peri_request_line(req, sizeof req, verb, words, nwords)) {
        // One word per argument: a space inside one (a quoted "a=1 b=2") is
        // refused, not split, so say that rather than blame the length.
        for (int i = 0; i < nwords; i++) {
            if (!peri_request_word_ok(words[i])) {
                fprintf(stderr, "peri: an argument cannot be empty or contain a space: pass each setting as its own argument\n");
                return 2;
            }
        }
        fprintf(stderr, "peri: request too long\n");
        return 2;
    }
    return forward(req);
}

// peri rgb get
// peri rgb set <key=value>...
// The daemon owns the keyboard: this only forwards the request and prints the reply.
static int cmd_rgb(int argc, char **argv)
{
    return forward_words("rgb", argv + 2, argc - 2);
}

// peri controls <module-id>: a camera's controls with their ranges and menu options (read-only).
static int cmd_controls(const char *id)
{
    char *const words[] = { (char *)id };
    return forward_words("controls", words, 1);
}

// peri fov <module-id> <query> [size]: one read of a camera's field-of-view extension unit; `peri fov <module-id> set <wide|medium|narrow|0-2>`
// writes it (the daemon reads its capabilities and current value first).
static int cmd_fov(int argc, char **argv)
{
    char *words[3] = { argv[2], argv[3], nullptr };
    int nwords = 2;
    if (argc == 5)
        words[nwords++] = argv[4];
    return forward_words("fov", words, nwords);
}

// peri status <module-id>: read what the module's device reports (read-only).
static int cmd_status(const char *id)
{
    char *const words[] = { (char *)id };
    return forward_words("status", words, 1);
}

// Everything `peri decode --scan` found, kept for the summary.
struct decode_state {
    struct razer_found *frames;
    size_t count, capacity;
    bool summary;
};

static void decode_collect(const struct razer_found *found, void *user)
{
    struct decode_state *s = user;
    char line[320];
    if (!s->summary) {
        razer_describe(found, line, sizeof line);
        printf("packet %zu: %s\n", found->packet, line);
    }
    if (s->count < s->capacity)
        s->frames[s->count++] = *found;
}

// The distinct frames, each once with how often it was seen and a sample. A frame is distinct by its
// command, direction, size and first two arguments (storage and LED id for lighting), so a new
// variant of a known command is listed on its own and counted apart from a new command.
static void decode_summary(const struct decode_state *s)
{
    size_t unknown = 0, variants = 0;
    for (size_t i = 0; i < s->count; i++) {
        const struct razer_found *f = &s->frames[i];
        bool seen = false, command_seen = false;
        for (size_t j = 0; j < i; j++) {
            seen = seen || razer_same_variant(&s->frames[j], f);
            command_seen = command_seen || razer_same_command(&s->frames[j], f);
        }
        if (seen)
            continue;
        size_t times = 0;
        for (size_t j = i; j < s->count; j++)
            if (razer_same_variant(&s->frames[j], f))
                times++;
        char line[320];
        razer_describe(f, line, sizeof line);
        bool known = razer_command_name(f->frame[7], f->frame[8]) != nullptr;
        printf("%4zux  %s%s\n", times, line, known && command_seen ? "   <- new variant" : "");
        if (!known && !command_seen)
            unknown++;
        else if (command_seen)
            variants++;
    }
    printf("%zu frame%s, %zu command%s we do not know yet, %zu more variant%s of commands already listed\n",
           s->count, s->count == 1 ? "" : "s", unknown, unknown == 1 ? "" : "s", variants,
           variants == 1 ? "" : "s");
}

// peri decode <hex bytes>           describe one frame (64 bytes, report id 07 first)
// peri decode --scan <file|-> [--summary]
//                                   find every Razer frame in a Wireshark text dump (`tshark -x`)
// Reads text only: it never opens a device. See docs/capturing-synapse.md.
static int cmd_decode(int argc, char **argv)
{
    if (argc >= 4 && strcmp(argv[2], "--scan") == 0) {
        FILE *f = strcmp(argv[3], "-") == 0 ? stdin : fopen(argv[3], "r");
        if (!f) {
            fprintf(stderr, "peri: %s: %s\n", argv[3], strerror(errno));
            return 1;
        }
        size_t cap = 1 << 16, len = 0;
        char *text = malloc(cap);
        for (size_t n; text && (n = fread(text + len, 1, cap - len - 1, f)) > 0;) {
            len += n;
            if (len + 1 >= cap) {
                if (cap >= (64u << 20)) {
                    fprintf(stderr, "peri: the dump is larger than 64 MiB\n");
                    free(text);
                    return 1;
                }
                char *bigger = realloc(text, cap *= 2);
                if (!bigger) {
                    free(text);
                    return 1;
                }
                text = bigger;
            }
        }
        if (f != stdin)
            fclose(f);
        if (!text)
            return 1;
        text[len] = '\0';

        struct decode_state s = { .frames = calloc(8192, sizeof *s.frames), .capacity = 8192,
                                  .summary = argc >= 5 && strcmp(argv[4], "--summary") == 0 };
        if (!s.frames) {
            free(text);
            return 1;
        }
        size_t n = razer_scan_dump(text, decode_collect, &s);
        if (s.summary)
            decode_summary(&s);
        else if (n == 0)
            puts("no Razer frames found");
        free(s.frames);
        free(text);
        return 0;
    }

    char joined[512] = "";
    for (int i = 2; i < argc; i++)
        snprintf(joined + strlen(joined), sizeof joined - strlen(joined), "%s ", argv[i]);
    struct razer_found found = { .packet = 1 };
    if (argc < 3 || razer_parse_hex(joined, found.frame, sizeof found.frame) != RAZER_REPORT_LEN
        || found.frame[0] != RAZER_REPORT_ID) {
        fprintf(stderr, "peri: decode takes the 64 bytes of one frame as hex, starting with 07, "
                        "or `--scan <file|-> [--summary]`\n");
        return 2;
    }
    uint8_t x = 0, y = 0;
    for (int i = 1; i <= 62; i++)
        x ^= found.frame[i];
    for (int i = 2; i <= 61; i++)
        y ^= found.frame[i];
    found.request_crc = x == found.frame[63] && found.frame[1] == 0x00;
    found.reply_crc = y == found.frame[62] && found.frame[1] != 0x00;
    char line[320];
    razer_describe(&found, line, sizeof line);
    printf("%s\n", line);
    if (!found.request_crc && !found.reply_crc)
        printf("note: the checksum fits neither the request rule (byte 63) nor the reply rule (byte 62)\n");
    return 0;
}

// peri look <module-id> get|save|apply <unmuted|muted> [effect=<name>] [color=<rrggbb>]
//           [color2=<rrggbb>] [speed=<slow|fast>] [brightness=<0-255>]
// A module keeps two saved looks. get and save only touch the saved file; apply is the one that
// sends the look to the device.
static int cmd_look(int argc, char **argv)
{
    return forward_words("look", argv + 2, argc - 2);
}

// peri audio <module-id> get
// peri audio <module-id> set gain=<0-100> monitor=<0-100> monitor_on=<yes|no> headphones=<0-100>
// The microphone's standard USB audio controls (capture gain, monitor mix, headphone volume).
static int cmd_audio(int argc, char **argv)
{
    return forward_words("audio", argv + 2, argc - 2);
}

// peri set <module-id> brightness=<0-255>
// peri set <module-id> effect=<none|static|breathing|spectrum> [color=<rrggbb>]
// The daemon checks every setting. A set may be kept in the device's memory (the Seiren's stored
// brightness followed a "live only" set), so do not drive this from a slider drag.
static int cmd_set(int argc, char **argv)
{
    return forward_words("set", argv + 2, argc - 2);
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

    if ((argc == 4 || argc == 5) && strcmp(argv[1], "feature") == 0)
        return cmd_feature(argv[2], argv[3], argc == 5 ? argv[4] : nullptr);

    if (argc == 4 && strcmp(argv[1], "razer") == 0)
        return cmd_razer(argv[2], argv[3]);

    if (argc == 4 && strcmp(argv[1], "razer90") == 0)
        return cmd_razer90(argv[2], argv[3]);

    if (argc >= 2 && strcmp(argv[1], "modules") == 0)
        return cmd_modules(argc, argv);
    if (argc == 3 && strcmp(argv[1], "use") == 0)
        return cmd_module_verb("use", argv[2]);
    if (argc == 3 && strcmp(argv[1], "remove") == 0)
        return cmd_module_verb("remove", argv[2]);
    if (argc == 3 && strcmp(argv[1], "blob") == 0)
        return cmd_blob(argv[2]);
    if (argc == 4 && strcmp(argv[1], "xap") == 0)
        return cmd_xap(argv[2], argv[3]);
    if (argc >= 3 && strcmp(argv[1], "rgb") == 0)
        return cmd_rgb(argc, argv);
    if (argc == 3 && strcmp(argv[1], "status") == 0)
        return cmd_status(argv[2]);
    if (argc == 3 && strcmp(argv[1], "controls") == 0)
        return cmd_controls(argv[2]);
    if ((argc == 4 || argc == 5) && strcmp(argv[1], "fov") == 0)
        return cmd_fov(argc, argv);
    if (argc >= 3 && strcmp(argv[1], "decode") == 0)
        return cmd_decode(argc, argv);
    if (argc >= 4 && argc <= 8 && strcmp(argv[1], "audio") == 0)
        return cmd_audio(argc, argv);
    if (argc >= 5 && argc <= 10 && strcmp(argv[1], "look") == 0)
        return cmd_look(argc, argv);
    if (argc >= 4 && argc <= 7 && strcmp(argv[1], "set") == 0)
        return cmd_set(argc, argv);

    fprintf(stderr, "usage: peri <setup [--no-udev]|ping|list [--all] [--json]|modules [--json]|use <id>|remove <id>|monitor <node>|feature <node> <report-hex> [length]|razer <node> firmware|brightness|led-effect|led-rgb|razer90 <node> firmware|battery|charging|dpi|poll-rate|xap <node> <route-hex>|blob <node>|status <module-id>|controls <module-id>|fov <module-id> <cur|min|max|res|len|info|def> [size]|fov <module-id> set <wide|medium|narrow|0-2>|decode <hex>|--scan <file> [--summary]|audio <module-id> get|set gain=<n> monitor=<n> monitor_on=<yes|no> headphones=<n>|look <module-id> get|save|apply <unmuted|muted> [effect=<name>] [color=<rrggbb>] [color2=<rrggbb>] [speed=<slow|fast>] [brightness=<n>]|set <module-id> brightness=<0-255>|dpi=<n>|poll=<125|500|1000>|<camera-control>=<n>|effect=<name> [color=<rrggbb>] [color2=<rrggbb>] [speed=<slow|fast>]|rgb get|set <key=value>...|save>\n");
    return 2;
}
