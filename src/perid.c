// perid: the peri core daemon. For now it only answers "ping".

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "controls.h"
#include "inventory.h"
#include "module.h"
#include "rgb_cmd.h"
#include "device_cmd.h"
#include "scan.h"
#include "udev.h"

static volatile sig_atomic_t stop;

static void on_signal(int)
{
    stop = 1;
}

// One interface per line: "<node> <vid>:<pid> <usage page>:<usage> <hidden> <group> <name>"
// hidden is "-", "fido" or "camera". Clients decide what to show; nothing is left out here.
static void send_list(int client)
{
    struct peri_device devs[32];
    size_t count = peri_scan(devs, 32);

    char buf[8192];
    size_t len = 0;
    for (size_t i = 0; i < count; i++) {
        const struct peri_device *d = &devs[i];
        int n = snprintf(buf + len, sizeof buf - len, "%s %04x:%04x %04x:%04x %s %s %s\n",
                         d->node, d->vid, d->pid, d->usage_page, d->usage,
                         peri_hidden_name(d->hidden), d->group, d->name);
        if (n < 0 || (size_t)n >= sizeof buf - len)
            break;
        len += (size_t)n;
    }
    (void)!write(client, buf, len);
}

// One physical device per line:
// "<vid>:<pid> <installed|available|none> <module or -> <hidden: - fido camera> <group> <name>"
// Hidden devices are included, with their reason; clients decide what to show.
static void send_devices(int client)
{
    struct peri_device devs[32];
    size_t dev_count = peri_scan(devs, 32);

    char dir[PATH_MAX];
    struct peri_module catalog[32], installed[32];
    size_t catalog_count = 0, installed_count = 0;
    if (peri_catalog_dir(dir, sizeof dir))
        catalog_count = peri_modules_scan(dir, catalog, 32);
    if (peri_installed_dir(dir, sizeof dir))
        installed_count = peri_modules_scan(dir, installed, 32);

    struct peri_item items[32];
    size_t count = peri_inventory(devs, dev_count, installed, installed_count, catalog,
                                  catalog_count, items, 32);

    char buf[8192];
    size_t len = 0;
    for (size_t i = 0; i < count; i++) {
        const struct peri_item *it = &items[i];
        int n = snprintf(buf + len, sizeof buf - len, "%04x:%04x %s %s %s %s %s\n", it->vid,
                         it->pid, peri_status_name(it->status), it->module[0] ? it->module : "-",
                         peri_hidden_name(it->hidden), it->group, it->name);
        if (n < 0 || (size_t)n >= sizeof buf - len)
            break;
        len += (size_t)n;
    }
    (void)!write(client, buf, len);
}

static bool has_module(const struct peri_module *list, size_t count, const char *id)
{
    for (size_t i = 0; i < count; i++)
        if (strcmp(list[i].id, id) == 0)
            return true;
    return false;
}

// One module per line: "<id> <installed|available> <access> <engine> <vid>:<pid> <version> <name>".
// access is "ok" when the installed udev rule already covers the module's device, "setup"
// when `peri setup` has to update the rule, and "-" for modules that are not installed.
// Everything in the bundled catalog, plus anything installed that is not in it.
static void send_modules(int client)
{
    char dir[PATH_MAX];
    struct peri_module catalog[32], installed[32];
    size_t catalog_count = 0, installed_count = 0;
    if (peri_catalog_dir(dir, sizeof dir))
        catalog_count = peri_modules_scan(dir, catalog, 32);
    if (peri_installed_dir(dir, sizeof dir))
        installed_count = peri_modules_scan(dir, installed, 32);

    char *rules = peri_udev_read_installed();
    char buf[4096];
    size_t len = 0;
    for (size_t i = 0; i < catalog_count + installed_count; i++) {
        bool from_catalog = i < catalog_count;
        const struct peri_module *m = from_catalog ? &catalog[i] : &installed[i - catalog_count];
        if (!from_catalog && has_module(catalog, catalog_count, m->id))
            continue;
        bool is_installed = has_module(installed, installed_count, m->id);
        const char *access = !is_installed ? "-" : rules && peri_udev_covers(rules, m) ? "ok" : "setup";
        int n = snprintf(buf + len, sizeof buf - len, "%s %s %s %s %04x:%04x %s %s\n", m->id,
                         is_installed ? "installed" : "available", access, m->engine, m->vid,
                         m->pid, m->version, m->name);
        if (n < 0 || (size_t)n >= sizeof buf - len)
            break;
        len += (size_t)n;
    }
    free(rules);
    (void)!write(client, buf, len);
}

// "use <id>" / "remove <id>": exactly one valid id and nothing after it.
static void module_verb(int client, bool use, char *args)
{
    char reply[256], err[200] = "";
    char *rest;
    const char *id = strtok_r(args, " \n", &rest);
    char dir[PATH_MAX], catalog[PATH_MAX];

    if (!id || strtok_r(nullptr, " \n", &rest))
        snprintf(reply, sizeof reply, "err usage: %s <module-id>\n", use ? "use" : "remove");
    else if (!peri_module_id_valid(id))
        snprintf(reply, sizeof reply, "err '%.48s' is not a valid module id\n", id);
    else if (!peri_installed_dir(dir, sizeof dir))
        snprintf(reply, sizeof reply, "err neither XDG_DATA_HOME nor HOME is set\n");
    else if (use) {
        struct peri_module m;
        if (!peri_catalog_dir(catalog, sizeof catalog)
            || !peri_module_install(catalog, dir, id, &m, err, sizeof err))
            snprintf(reply, sizeof reply, "err %s\n", err[0] ? err : "no catalog found");
        else
            snprintf(reply, sizeof reply, "ok installed %s %s\n", m.id, m.version);
    } else if (!peri_module_remove(dir, id, err, sizeof err))
        snprintf(reply, sizeof reply, "err %s\n", err);
    else
        snprintf(reply, sizeof reply, "ok removed %s\n", id);
    (void)!write(client, reply, strlen(reply));
}

static void handle(int client)
{
    // Don't let a silent client wedge the single-threaded loop.
    struct timeval timeout = { .tv_sec = 1 };
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

    char req[256];
    ssize_t n = read(client, req, sizeof req - 1);
    if (n <= 0)
        return;
    req[n] = '\0';

    if (strncmp(req, "list", 4) == 0) {
        send_list(client);
        return;
    }
    if (strncmp(req, "devices", 7) == 0) {
        send_devices(client);
        return;
    }
    if (strncmp(req, "modules", 7) == 0) {
        send_modules(client);
        return;
    }
    if (strncmp(req, "use ", 4) == 0 || strncmp(req, "remove ", 7) == 0) {
        bool use = req[0] == 'u';
        module_verb(client, use, req + (use ? 4 : 7));
        controls_detach(); // a module came or went: the reader decides again
        controls_rescan();
        return;
    }
    if (strncmp(req, "rgb ", 4) == 0) {
        char reply[1024];
        rgb_handle(req + 4, reply, sizeof reply);
        (void)!write(client, reply, strlen(reply));
        return;
    }
    if (strncmp(req, "audio ", 6) == 0) {
        char reply[256];
        audio_handle(req + 6, reply, sizeof reply);
        (void)!write(client, reply, strlen(reply));
        return;
    }
    if (strncmp(req, "look ", 5) == 0) {
        char reply[256];
        look_handle(req + 5, reply, sizeof reply);
        (void)!write(client, reply, strlen(reply));
        return;
    }
    if (strncmp(req, "status ", 7) == 0 || strncmp(req, "set ", 4) == 0) {
        char reply[256];
        if (req[0] == 's' && req[1] == 't')
            status_handle(req + 7, reply, sizeof reply);
        else
            set_handle(req + 4, reply, sizeof reply);
        (void)!write(client, reply, strlen(reply));
        return;
    }
    const char *reply = strncmp(req, "ping", 4) == 0 ? "pong\n" : "err unknown command\n";
    (void)!write(client, reply, strlen(reply));
}

int main(void)
{
    struct sockaddr_un addr;
    if (peri_socket_addr(&addr) < 0) {
        fprintf(stderr, "perid: XDG_RUNTIME_DIR is not set or too long\n");
        return 1;
    }

    // No SA_RESTART: a signal must interrupt accept() so we can exit.
    struct sigaction sa = { .sa_handler = on_signal };
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("perid: socket");
        return 1;
    }

    unlink(addr.sun_path);
    umask(077);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 || listen(fd, 8) < 0) {
        perror("perid: bind/listen");
        return 1;
    }
    fprintf(stderr, "perid: listening on %s\n", addr.sun_path);

    // One thread: wait for a client or for the microphone's controls, whichever speaks first.
    // While no device is attached, wake every few seconds to look for one.
    controls_rescan();
    while (!stop) {
        struct pollfd fds[2] = { { .fd = fd, .events = POLLIN },
                                 { .fd = controls_fd(), .events = POLLIN } };
        int ready = poll(fds, 2, controls_fd() < 0 ? 3000 : -1);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("perid: poll");
            break;
        }
        if (controls_fd() >= 0 && fds[1].revents)
            controls_service(fds[1].revents);
        controls_rescan(); // no-op while attached

        if (!(fds[0].revents & POLLIN))
            continue;
        int client = accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            perror("perid: accept");
            break;
        }
        handle(client);
        close(client);
    }
    controls_detach();

    unlink(addr.sun_path);
    close(fd);
    return 0;
}
