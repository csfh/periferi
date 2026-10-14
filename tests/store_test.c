#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/store.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static int rm_entry(const char *path, const struct stat *, int, struct FTW *)
{
    return remove(path);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

#define ID "razer-seiren-v3-chroma"

int main(void)
{
    char root[] = "/tmp/peri-store-test-XXXXXX";
    if (!mkdtemp(root))
        return 2;
    setenv("XDG_CONFIG_HOME", root, 1);

    char dir[512], file[600];
    CHECK(peri_look_dir(dir, sizeof dir));
    snprintf(file, sizeof file, "%s/peri/state/" ID ".toml", root);
    CHECK(strstr(dir, "/peri/state") != nullptr && strncmp(dir, root, strlen(root)) == 0);

    // Nothing saved yet: unknown, not "off".
    struct peri_look l = peri_look_load(ID);
    CHECK(l.effect[0] == '\0' && l.color[0] == '\0');

    // A static look keeps its colour, lowercased so it equals a preset.
    CHECK(peri_look_save(ID, "static", "FF00aa"));
    l = peri_look_load(ID);
    CHECK(strcmp(l.effect, "static") == 0 && strcmp(l.color, "ff00aa") == 0);

    // Any other effect clears the colour, or an old swatch would stay highlighted.
    CHECK(peri_look_save(ID, "breathing", "0000ff"));
    l = peri_look_load(ID);
    CHECK(strcmp(l.effect, "breathing") == 0 && l.color[0] == '\0');
    CHECK(peri_look_save(ID, "none", nullptr));
    l = peri_look_load(ID);
    CHECK(strcmp(l.effect, "none") == 0 && l.color[0] == '\0'); // "none" is a real effect

    // Bad input is refused and leaves the record as it was.
    CHECK(!peri_look_save(ID, "static", nullptr));
    CHECK(!peri_look_save(ID, "static", "00ff"));
    CHECK(!peri_look_save(ID, "static", "zz0000"));
    CHECK(!peri_look_save(ID, "rainbow", "0000ff"));
    CHECK(!peri_look_save(ID, nullptr, nullptr));
    CHECK(!peri_look_save("../evil", "none", nullptr));
    CHECK(!peri_look_save("Bad Id", "none", nullptr));
    l = peri_look_load(ID);
    CHECK(strcmp(l.effect, "none") == 0);
    char outside[600];
    snprintf(outside, sizeof outside, "%s/peri/evil.toml", root);
    CHECK(access(outside, F_OK) != 0);

    // No temp file is left behind.
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s/peri/state/." ID ".toml.tmp", root);
    CHECK(access(tmp, F_OK) != 0);

    // A file that is not a look this program wrote counts as unknown, never as an error.
    write_file(file, "this is not toml = = =\n");
    l = peri_look_load(ID);
    CHECK(l.effect[0] == '\0');
    write_file(file, "effect = \"rainbow\"\n");
    l = peri_look_load(ID);
    CHECK(l.effect[0] == '\0');
    write_file(file, "effect = \"static\"\n"); // static needs a colour
    l = peri_look_load(ID);
    CHECK(l.effect[0] == '\0');
    write_file(file, "effect = \"static\"\ncolor = \"nothex\"\n");
    l = peri_look_load(ID);
    CHECK(l.effect[0] == '\0');
    write_file(file, "effect = 5\n");
    l = peri_look_load(ID);
    CHECK(l.effect[0] == '\0');
    write_file(file, "effect = \"spectrum\"\ncolor = \"0000ff\"\n"); // a stray colour is ignored
    l = peri_look_load(ID);
    CHECK(strcmp(l.effect, "spectrum") == 0 && l.color[0] == '\0');
    write_file(file, "");
    l = peri_look_load(ID);
    CHECK(l.effect[0] == '\0');

    // A symlink is not followed, and an invalid id never touches the disk.
    unlink(file);
    CHECK(symlink("/etc/hostname", file) == 0);
    l = peri_look_load(ID);
    CHECK(l.effect[0] == '\0');
    l = peri_look_load("../x");
    CHECK(l.effect[0] == '\0');

    // Without XDG_CONFIG_HOME the home directory is used.
    char home[] = "/tmp/peri-store-home-XXXXXX";
    if (!mkdtemp(home))
        return 2;
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", home, 1);
    CHECK(peri_look_dir(dir, sizeof dir));
    char want[600];
    snprintf(want, sizeof want, "%s/.config/peri/state", home);
    CHECK(strcmp(dir, want) == 0);
    CHECK(peri_look_save(ID, "spectrum", nullptr));
    l = peri_look_load(ID);
    CHECK(strcmp(l.effect, "spectrum") == 0);

    nftw(root, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
    nftw(home, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
    return failures ? 1 : 0;
}
