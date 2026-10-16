#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/module.h"
#include "../src/scan.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

// A module file with one thing replaced, to test a single rule at a time.
static const char good[] =
    "schema = 1\nid = \"my-mod\"\nname = \"My mod\"\nversion = \"1.0.0\"\nengine = \"xap\"\n"
    "[match]\nvid = 0x1234\npid = 0x5678\nusage_page = 0xff51\nusage = 0x58\n"
    "[capabilities]\nrgb = true\n";

static bool parses(const char *text, char *err, size_t err_size)
{
    struct peri_module m;
    return peri_module_parse(text, strlen(text), &m, err, err_size);
}

// Replace the first occurrence of `from` in `good` with `to`; true if the result is rejected.
static bool rejected_with(const char *from, const char *to, const char *expect_in_error)
{
    char text[1024], err[200] = "";
    const char *at = strstr(good, from);
    if (!at) {
        printf("FAIL test setup: '%s' not in the good module\n", from);
        failures++;
        return false;
    }
    snprintf(text, sizeof text, "%.*s%s%s", (int)(at - good), good, to, at + strlen(from));
    bool bad = !parses(text, err, sizeof err);
    if (bad && !strstr(err, expect_in_error)) {
        printf("FAIL rejected, but the error '%s' lacks '%s'\n", err, expect_in_error);
        failures++;
    }
    return bad;
}

static void test_select(void)
{
    struct peri_module mods[2] = {
        { .id = "ctrl", .vid = 0x359b, .pid = 9, .has_usage = true, .usage_page = 0xff51, .usage = 0x58 },
        { .id = "mic", .vid = 0x1532, .pid = 0x56f, .has_usage = true, .usage_page = 0xff53, .usage = 4 },
    };
    struct peri_device devs[3] = {
        { .node = "hidraw1", .vid = 0x1532, .pid = 0x56f, .usage_page = 0xff53, .usage = 4 },
        { .node = "hidraw5", .vid = 0x359b, .pid = 9, .usage_page = 0xff51, .usage = 0x58 },
        { .node = "hidraw6", .vid = 0x359b, .pid = 9, .usage_page = 1, .usage = 6 }, // other interface
    };
    char node[32];
    const struct peri_module *picked = nullptr;

    // Chosen by module id, whatever the node numbers are.
    CHECK(peri_module_select(devs, 3, mods, 2, "mic", node, sizeof node, &picked) == PERI_SELECT_OK);
    CHECK(strcmp(node, "hidraw1") == 0 && picked == &mods[1]);
    CHECK(peri_module_select(devs, 3, mods, 2, "ctrl", node, sizeof node, nullptr) == PERI_SELECT_OK);
    CHECK(strcmp(node, "hidraw5") == 0);

    CHECK(peri_module_select(devs, 3, mods, 2, "nope", node, sizeof node, nullptr)
          == PERI_SELECT_NOT_INSTALLED);
    CHECK(peri_module_select(devs, 1, mods, 2, "ctrl", node, sizeof node, nullptr)
          == PERI_SELECT_NOT_CONNECTED);

    // Two matching devices: refuse to guess.
    devs[2] = devs[0];
    strcpy(devs[2].node, "hidraw9");
    CHECK(peri_module_select(devs, 3, mods, 2, "mic", node, sizeof node, nullptr)
          == PERI_SELECT_AMBIGUOUS);

    // A security key never matches, even on an exact ID.
    devs[2].hidden = PERI_HIDDEN_FIDO;
    CHECK(peri_module_select(devs, 3, mods, 2, "mic", node, sizeof node, nullptr) == PERI_SELECT_OK);
}

static void test_matches(void)
{
    struct peri_module m = { .vid = 0x359b, .pid = 9, .has_usage = true,
                             .usage_page = 0xff51, .usage = 0x58 };
    struct peri_device d = { .vid = 0x359b, .pid = 9, .usage_page = 0xff51, .usage = 0x58 };
    CHECK(peri_module_matches(&m, &d));
    d.vid = 0x1532;
    CHECK(!peri_module_matches(&m, &d));
    d.vid = 0x359b;
    d.pid = 10;
    CHECK(!peri_module_matches(&m, &d));
    d.pid = 9;
    d.usage_page = 1;
    CHECK(!peri_module_matches(&m, &d));
    d.usage_page = 0xff51;
    d.usage = 0x59;
    CHECK(!peri_module_matches(&m, &d));
    d.usage = 0x58;
    d.hidden = PERI_HIDDEN_FIDO; // no module claims a security key, even on an exact match
    CHECK(!peri_module_matches(&m, &d));
    d.hidden = PERI_VISIBLE;
    m.has_usage = false; // a module that names no interface matches none
    CHECK(!peri_module_matches(&m, &d));
}

static int rm_entry(const char *path, const struct stat *, int, struct FTW *)
{
    return remove(path);
}

static bool exists(const char *dir, const char *rest)
{
    char path[PATH_MAX];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, rest);
    return lstat(path, &st) == 0;
}

static void test_install_remove(void)
{
    char root[PATH_MAX], installed[PATH_MAX], err[200] = "", path[PATH_MAX];
    const char *tmp = getenv("TMPDIR");
    snprintf(root, sizeof root, "%s/peri-test-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(root)) {
        printf("FAIL mkdtemp\n");
        failures++;
        return;
    }
    snprintf(installed, sizeof installed, "%s/data/peri/modules", root); // parents do not exist yet
    struct peri_module m;

    // Install from the real catalog: it validates, lands, and the scan finds it.
    CHECK(peri_module_install(CATALOG_DIR, installed, "drop-ctrl-v2", &m, err, sizeof err));
    CHECK(strcmp(m.id, "drop-ctrl-v2") == 0);
    CHECK(exists(installed, "drop-ctrl-v2/module.toml"));
    CHECK(!exists(installed, "drop-ctrl-v2/.module.toml.tmp"));
    struct peri_module found[4];
    CHECK(peri_modules_scan(installed, found, 4) == 1 && strcmp(found[0].id, "drop-ctrl-v2") == 0);
    // Installing again replaces the copy.
    CHECK(peri_module_install(CATALOG_DIR, installed, "drop-ctrl-v2", nullptr, err, sizeof err));

    // The installed copy is byte for byte the catalog entry.
    snprintf(path, sizeof path, "%s/drop-ctrl-v2/module.toml", installed);
    char a[4096], b[4096];
    FILE *fa = fopen(CATALOG_DIR "/drop-ctrl-v2/module.toml", "r"), *fb = fopen(path, "r");
    size_t na = fa ? fread(a, 1, sizeof a, fa) : 1, nb = fb ? fread(b, 1, sizeof b, fb) : 2;
    CHECK(na == nb && memcmp(a, b, na) == 0);
    if (fa)
        fclose(fa);
    if (fb)
        fclose(fb);

    // Bad ids and unknown modules fail and create nothing.
    CHECK(!peri_module_install(CATALOG_DIR, installed, "nope", nullptr, err, sizeof err));
    CHECK(strstr(err, "catalog"));
    CHECK(!exists(installed, "nope"));
    CHECK(!peri_module_install(CATALOG_DIR, installed, "../x", nullptr, err, sizeof err));
    CHECK(!exists(installed, "../x") && !exists(installed, "x"));
    // A catalog entry that does not validate is not installed.
    CHECK(!peri_module_install(FIXTURES_DIR, installed, "broken", nullptr, err, sizeof err));
    CHECK(!exists(installed, "broken/module.toml"));
    CHECK(!peri_module_install(FIXTURES_DIR, installed, "wrong-dir", nullptr, err, sizeof err));
    CHECK(strstr(err, "has id") && !exists(installed, "wrong-dir/module.toml"));

    // Remove deletes the directory; a second remove says it is not installed.
    CHECK(peri_module_remove(installed, "drop-ctrl-v2", err, sizeof err));
    CHECK(!exists(installed, "drop-ctrl-v2"));
    CHECK(!peri_module_remove(installed, "drop-ctrl-v2", err, sizeof err) && strstr(err, "not installed"));
    CHECK(!peri_module_remove(installed, "../x", err, sizeof err));
    CHECK(!peri_module_remove(root, "never", err, sizeof err));

    // Extra files in the directory: refuse, and leave everything there.
    CHECK(peri_module_install(CATALOG_DIR, installed, "drop-ctrl-v2", nullptr, err, sizeof err));
    snprintf(path, sizeof path, "%s/drop-ctrl-v2/extra.so", installed);
    FILE *f = fopen(path, "w");
    if (f)
        fclose(f);
    CHECK(!peri_module_remove(installed, "drop-ctrl-v2", err, sizeof err) && strstr(err, "besides"));
    CHECK(exists(installed, "drop-ctrl-v2/extra.so") && exists(installed, "drop-ctrl-v2/module.toml"));

    // A symlinked <id> is not followed: refuse, and the target survives.
    char target[PATH_MAX], link[PATH_MAX];
    snprintf(target, sizeof target, "%s/elsewhere", root);
    CHECK(mkdir(target, 0755) == 0);
    snprintf(path, sizeof path, "%s/module.toml", target);
    f = fopen(path, "w");
    if (f)
        fclose(f);
    snprintf(link, sizeof link, "%s/linked", installed);
    CHECK(symlink(target, link) == 0);
    CHECK(!peri_module_remove(installed, "linked", err, sizeof err));
    CHECK(exists(root, "elsewhere/module.toml"));

    // Install does not write through a symlinked <id> either.
    char installed2[PATH_MAX];
    snprintf(installed2, sizeof installed2, "%s/data2", root);
    snprintf(target, sizeof target, "%s/t2", root);
    CHECK(mkdir(installed2, 0755) == 0 && mkdir(target, 0755) == 0);
    snprintf(link, sizeof link, "%s/drop-ctrl-v2", installed2);
    CHECK(symlink(target, link) == 0);
    CHECK(!peri_module_install(CATALOG_DIR, installed2, "drop-ctrl-v2", nullptr, err, sizeof err));
    CHECK(!exists(root, "t2/module.toml") && !exists(root, "t2/.module.toml.tmp"));

    nftw(root, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
}

int main(void)
{
    char err[200];
    struct peri_module m;

    // The good example parses, with every field.
    CHECK(peri_module_parse(good, strlen(good), &m, err, sizeof err));
    CHECK(strcmp(m.id, "my-mod") == 0 && strcmp(m.name, "My mod") == 0);
    CHECK(strcmp(m.version, "1.0.0") == 0 && strcmp(m.engine, "xap") == 0);
    CHECK(m.vid == 0x1234 && m.pid == 0x5678);
    CHECK(m.has_usage && m.usage_page == 0xff51 && m.usage == 0x58 && m.rgb);

    // The module that ships in the catalog is valid and says what we expect.
    CHECK(peri_module_load(CATALOG_DIR "/drop-ctrl-v2/module.toml", &m, err, sizeof err));
    CHECK(strcmp(m.id, "drop-ctrl-v2") == 0 && strcmp(m.engine, "xap") == 0);
    CHECK(m.vid == 0x359b && m.pid == 0x0009 && m.usage_page == 0xff51 && m.usage == 0x58);
    CHECK(m.rgb);
    CHECK(m.max_brightness == 191); // from the board's own blob, see the module file

    // The Seiren's module uses the other engine, "razer", and gives no capabilities yet.
    CHECK(peri_module_load(CATALOG_DIR "/razer-seiren-v3-chroma/module.toml", &m, err, sizeof err));
    CHECK(strcmp(m.id, "razer-seiren-v3-chroma") == 0 && strcmp(m.engine, "razer") == 0);
    CHECK(m.vid == 0x1532 && m.pid == 0x056f && m.usage_page == 0xff53 && m.usage == 0x04);
    CHECK(!m.rgb && m.max_brightness == 255); // the protocol's range, see the module file

    // The DeathAdder's module uses a third engine, "razer90", and matches the plain mouse usage:
    // its control item is inside the mouse collection, not on a vendor page.
    CHECK(peri_module_load(CATALOG_DIR "/razer-deathadder-v3-pro/module.toml", &m, err, sizeof err));
    CHECK(strcmp(m.id, "razer-deathadder-v3-pro") == 0 && strcmp(m.engine, "razer90") == 0);
    CHECK(m.vid == 0x1532 && m.pid == 0x00c3 && m.usage_page == 0x0001 && m.usage == 0x0002);
    CHECK(!m.rgb && m.max_brightness == 0 && m.min_dpi == 100 && m.max_dpi == 30000);

    // The BRIO's module uses the "v4l2" engine and is anchored on its one HID interface.
    CHECK(peri_module_load(CATALOG_DIR "/brio/module.toml", &m, err, sizeof err));
    CHECK(strcmp(m.id, "brio") == 0 && strcmp(m.engine, "v4l2") == 0);
    CHECK(m.vid == 0x046d && m.pid == 0x085e && m.usage_page == 0x000c && m.usage == 0x0001 && !m.rgb);

    // Ids double as path components: only [a-z0-9-], not at the edges.
    CHECK(peri_module_id_valid("drop-ctrl-v2") && peri_module_id_valid("a"));
    CHECK(!peri_module_id_valid("") && !peri_module_id_valid("-a") && !peri_module_id_valid("a-"));
    CHECK(!peri_module_id_valid("Upper") && !peri_module_id_valid("a/b"));
    CHECK(!peri_module_id_valid("..") && !peri_module_id_valid("a b"));
    CHECK(!peri_module_id_valid("123456789012345678901234567890123456789012345678901"));

    // Every rule rejects with a useful message.
    CHECK(rejected_with("schema = 1", "schema = 2", "schema"));
    CHECK(rejected_with("schema = 1\n", "", "schema"));
    CHECK(rejected_with("id = \"my-mod\"\n", "", "missing 'id'"));
    CHECK(rejected_with("\"my-mod\"", "\"../evil\"", "'id'"));
    CHECK(rejected_with("\"my-mod\"", "\"My-Mod\"", "'id'"));
    CHECK(rejected_with("\"my-mod\"", "5", "must be a string"));
    CHECK(rejected_with("\"My mod\"", "\"\"", "'name'"));
    CHECK(rejected_with("engine = \"xap\"", "engine = \"shell\"", "unknown engine"));
    CHECK(!rejected_with("engine = \"xap\"", "engine = \"razer90\"", "unknown engine"));
    CHECK(rejected_with("usage_page = 0xff51\n", "", "go together"));
    CHECK(rejected_with("usage = 0x58\n", "usage = 0x58\n[limits]\nmin_dpi = 100\n", "go together"));
    CHECK(rejected_with("usage = 0x58\n", "usage = 0x58\n[limits]\nmin_dpi = 900\nmax_dpi = 100\n",
                        "not below min"));
    CHECK(rejected_with("usage = 0x58\n", "usage = 0x58\n[limits]\nmin_dpi = 0\nmax_dpi = 100\n",
                        "at least 1"));
    CHECK(rejected_with("vid = 0x1234", "vid = 0", "vid"));
    CHECK(rejected_with("vid = 0x1234", "vid = 0x10000", "vid"));
    CHECK(rejected_with("vid = 0x1234", "vid = -1", "vid"));
    CHECK(rejected_with("vid = 0x1234\n", "", "missing 'match.vid'"));
    CHECK(rejected_with("pid = 0x5678", "pid = \"x\"", "pid"));
    CHECK(rejected_with("usage = 0x58\n", "", "go together"));
    CHECK(rejected_with("usage_page = 0xff51\nusage = 0x58\n", "", "needs"));
    CHECK(rejected_with("rgb = true", "rgb = 1", "capabilities.rgb"));

    // [limits] max_brightness: optional, 1-255, and 0 means "not given". The shipped CTRL
    // module carries the board's own limit (rgb_matrix.max_brightness in its config blob).
    {
        char text[1024];
        struct peri_module lim;
        CHECK(peri_module_parse(good, strlen(good), &lim, err, sizeof err) && lim.max_brightness == 0);
        snprintf(text, sizeof text, "%s[limits]\nmax_brightness = 191\n", good);
        CHECK(peri_module_parse(text, strlen(text), &lim, err, sizeof err) && lim.max_brightness == 191);
        snprintf(text, sizeof text, "%s[limits]\nmax_brightness = 255\n", good);
        CHECK(peri_module_parse(text, strlen(text), &lim, err, sizeof err) && lim.max_brightness == 255);
        snprintf(text, sizeof text, "%s[limits]\nmax_brightness = 1\n", good);
        CHECK(peri_module_parse(text, strlen(text), &lim, err, sizeof err) && lim.max_brightness == 1);
        const char *bad[] = { "0", "256", "-1", "\"191\"", "191.5", "true" };
        for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
            snprintf(text, sizeof text, "%s[limits]\nmax_brightness = %s\n", good, bad[i]);
            CHECK(!peri_module_parse(text, strlen(text), &lim, err, sizeof err));
            CHECK(strstr(err, "max_brightness") != nullptr);
        }
    }

    // Not TOML at all, and empty input.
    CHECK(!parses("this is = not [valid", err, sizeof err) && err[0]);
    CHECK(!parses("", err, sizeof err));

    // A module without capabilities is fine: rgb defaults to false.
    {
        char text[1024];
        const char *caps = strstr(good, "[capabilities]");
        snprintf(text, sizeof text, "%.*s", (int)(caps - good), good);
        CHECK(peri_module_parse(text, strlen(text), &m, err, sizeof err) && !m.rgb);
    }

    // Loading refuses a missing file.
    CHECK(!peri_module_load(FIXTURES_DIR "/nope/module.toml", &m, err, sizeof err));

    // Scanning keeps the one valid module whose directory matches its id and
    // skips the mismatched and the broken one (each logs a note on stderr).
    struct peri_module found[8];
    size_t count = peri_modules_scan(FIXTURES_DIR, found, 8);
    CHECK(count == 1);
    CHECK(count == 1 && strcmp(found[0].id, "ok-module") == 0 && found[0].vid == 0x1234);
    CHECK(peri_modules_scan(FIXTURES_DIR "/does-not-exist", found, 8) == 0);

    test_matches();
    test_select();
    test_install_remove();

    // The installed-module directory is the data base plus this suffix. The
    // base's own rules are tested against peri_xdg_path; this locks the suffix.
    unsetenv("XDG_DATA_HOME");
    setenv("HOME", "/tmp/peri-mod-home", 1);
    char dir[PATH_MAX];
    CHECK(peri_installed_dir(dir, sizeof dir));
    CHECK(strcmp(dir, "/tmp/peri-mod-home/.local/share/peri/modules") == 0);

    return failures ? 1 : 0;
}
