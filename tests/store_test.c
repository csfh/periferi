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
#define UN PERI_LOOK_UNMUTED
#define MU PERI_LOOK_MUTED

static struct peri_look look(const char *effect, const char *color, int brightness)
{
    struct peri_look l = PERI_LOOK_EMPTY;
    snprintf(l.effect, sizeof l.effect, "%s", effect);
    snprintf(l.color, sizeof l.color, "%s", color);
    l.brightness = brightness;
    return l;
}

static bool is_empty(struct peri_look l)
{
    return l.effect[0] == '\0' && l.color[0] == '\0' && l.brightness == -1;
}

int main(void)
{
    char root[] = "/tmp/peri-store-test-XXXXXX";
    if (!mkdtemp(root))
        return 2;
    setenv("XDG_CONFIG_HOME", root, 1);

    char dir[512], file[600];
    CHECK(peri_look_dir(dir, sizeof dir));
    snprintf(file, sizeof file, "%s/peri/state/" ID ".toml", root);
    CHECK(strncmp(dir, root, strlen(root)) == 0 && strstr(dir, "/peri/state") != nullptr);

    enum peri_look_which w;
    CHECK(peri_look_which_parse("unmuted", &w) && w == UN);
    CHECK(peri_look_which_parse("muted", &w) && w == MU);
    CHECK(!peri_look_which_parse("Muted", &w) && !peri_look_which_parse("", &w)
          && !peri_look_which_parse(nullptr, &w));
    CHECK(strcmp(peri_look_which_name(UN), "unmuted") == 0
          && strcmp(peri_look_which_name(MU), "muted") == 0);

    // Nothing saved yet: both looks are empty, which is not the effect "none" (Off).
    CHECK(is_empty(peri_look_load(ID, UN)) && is_empty(peri_look_load(ID, MU)));

    // The two looks are independent: saving one leaves the other as it was.
    struct peri_look blue = look("static", "0000FF", 102); // colour is kept lowercase
    CHECK(peri_look_save(ID, UN, &blue));
    struct peri_look l = peri_look_load(ID, UN);
    CHECK(strcmp(l.effect, "static") == 0 && strcmp(l.color, "0000ff") == 0 && l.brightness == 102);
    CHECK(is_empty(peri_look_load(ID, MU)));

    struct peri_look breath = look("breathing", "", -1);
    CHECK(peri_look_save(ID, MU, &breath));
    l = peri_look_load(ID, MU);
    CHECK(strcmp(l.effect, "breathing") == 0 && l.color[0] == '\0' && l.brightness == -1);
    l = peri_look_load(ID, UN); // untouched by the second save
    CHECK(strcmp(l.effect, "static") == 0 && strcmp(l.color, "0000ff") == 0 && l.brightness == 102);

    // Another effect drops the colour, or an old swatch would stay selected. "none" is a real effect.
    struct peri_look off = look("none", "ff0000", 0);
    CHECK(peri_look_save(ID, UN, &off));
    l = peri_look_load(ID, UN);
    CHECK(strcmp(l.effect, "none") == 0 && l.color[0] == '\0' && l.brightness == 0);

    // A look may hold only a brightness, or nothing at all.
    struct peri_look only_b = look("", "", 200);
    CHECK(peri_look_save(ID, UN, &only_b));
    l = peri_look_load(ID, UN);
    CHECK(l.effect[0] == '\0' && l.brightness == 200);
    struct peri_look nothing = PERI_LOOK_EMPTY;
    CHECK(peri_look_save(ID, UN, &nothing));
    CHECK(is_empty(peri_look_load(ID, UN)));
    l = peri_look_load(ID, MU);
    CHECK(strcmp(l.effect, "breathing") == 0);

    // Bad input is refused and leaves the file as it was.
    struct peri_look good = look("spectrum", "", 50);
    CHECK(peri_look_save(ID, UN, &good));
    struct peri_look bad_static = look("static", "", 10), bad_hex = look("static", "zz0000", 10),
                     short_hex = look("static", "00ff", 10), bad_effect = look("rainbow", "", 10),
                     bad_b = look("none", "", 300), neg_b = look("none", "", -5);
    CHECK(!peri_look_save(ID, UN, &bad_static) && !peri_look_save(ID, UN, &bad_hex)
          && !peri_look_save(ID, UN, &short_hex) && !peri_look_save(ID, UN, &bad_effect)
          && !peri_look_save(ID, UN, &bad_b) && !peri_look_save(ID, UN, &neg_b));
    CHECK(!peri_look_save(ID, UN, nullptr));
    CHECK(!peri_look_save("../evil", UN, &good) && !peri_look_save("Bad Id", UN, &good)
          && !peri_look_save(nullptr, UN, &good));
    l = peri_look_load(ID, UN);
    CHECK(strcmp(l.effect, "spectrum") == 0 && l.brightness == 50);
    char outside[600], tmp[600];
    snprintf(outside, sizeof outside, "%s/peri/evil.toml", root);
    snprintf(tmp, sizeof tmp, "%s/peri/state/." ID ".toml.tmp", root);
    CHECK(access(outside, F_OK) != 0 && access(tmp, F_OK) != 0);

    // Breathing and starlight take no colour, one, or two; a second needs a first.
    struct peri_look b2 = look("breathing", "00FF00", -1);
    snprintf(b2.color2, sizeof b2.color2, "%s", "FF0000");
    CHECK(peri_look_save(ID, UN, &b2));
    l = peri_look_load(ID, UN);
    CHECK(strcmp(l.effect, "breathing") == 0 && strcmp(l.color, "00ff00") == 0
          && strcmp(l.color2, "ff0000") == 0);
    struct peri_look s1 = look("starlight", "", -1);
    snprintf(s1.color2, sizeof s1.color2, "%s", "ff0000");      // no first colour: the second is dropped
    CHECK(peri_look_save(ID, UN, &s1));
    l = peri_look_load(ID, UN);
    CHECK(strcmp(l.effect, "starlight") == 0 && l.color[0] == '\0' && l.color2[0] == '\0');
    struct peri_look st2 = look("static", "0000ff", -1);
    snprintf(st2.color2, sizeof st2.color2, "%s", "ff0000");    // static takes one
    CHECK(peri_look_save(ID, UN, &st2));
    l = peri_look_load(ID, UN);
    CHECK(strcmp(l.color, "0000ff") == 0 && l.color2[0] == '\0');
    struct peri_look sp = look("spectrum", "0000ff", -1);
    snprintf(sp.color2, sizeof sp.color2, "%s", "ff0000");      // takes none
    CHECK(peri_look_save(ID, UN, &sp));
    l = peri_look_load(ID, UN);
    CHECK(l.color[0] == '\0' && l.color2[0] == '\0');
    struct peri_look bad2 = look("breathing", "0000ff", -1);
    snprintf(bad2.color2, sizeof bad2.color2, "%s", "zzzzzz");  // a bad colour is refused
    CHECK(!peri_look_save(ID, UN, &bad2));
    struct peri_look bad1 = look("breathing", "zz", -1);
    CHECK(!peri_look_save(ID, UN, &bad1));
    CHECK(strcmp(peri_look_load(ID, UN).effect, "spectrum") == 0); // the refused saves changed nothing

    // The new effect names are accepted, the reversed ones too, and nothing else.
    const char *names[] = { "wave", "wave-reverse", "wheel", "wheel-reverse", "starlight" };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        struct peri_look n = look(names[i], "", -1);
        CHECK(peri_look_save(ID, MU, &n));
        CHECK(strcmp(peri_look_load(ID, MU).effect, names[i]) == 0);
    }
    struct peri_look nope = look("fire", "", -1);
    CHECK(!peri_look_save(ID, MU, &nope));

    // The old single-look format (what an earlier version wrote) loads as two empty looks, and
    // the first save replaces it.
    write_file(file, "effect = \"none\"\n");
    CHECK(is_empty(peri_look_load(ID, UN)) && is_empty(peri_look_load(ID, MU)));
    write_file(file, "effect = \"static\"\ncolor = \"0000ff\"\n");
    CHECK(is_empty(peri_look_load(ID, UN)));
    CHECK(peri_look_save(ID, MU, &good));
    CHECK(strcmp(peri_look_load(ID, MU).effect, "spectrum") == 0 && is_empty(peri_look_load(ID, UN)));

    // A file that is not what this program wrote counts as unknown, never as an error. A bad
    // field spoils only itself.
    write_file(file, "this is not toml = = =\n");
    CHECK(is_empty(peri_look_load(ID, UN)) && is_empty(peri_look_load(ID, MU)));
    write_file(file, "");
    CHECK(is_empty(peri_look_load(ID, UN)));
    write_file(file, "[unmuted]\neffect = \"rainbow\"\nbrightness = 70\n");
    l = peri_look_load(ID, UN);
    CHECK(l.effect[0] == '\0' && l.brightness == 70);
    write_file(file, "[unmuted]\neffect = \"static\"\n"); // static needs a colour
    CHECK(peri_look_load(ID, UN).effect[0] == '\0');
    write_file(file, "[unmuted]\neffect = \"static\"\ncolor = \"nothex\"\nbrightness = 9\n");
    l = peri_look_load(ID, UN);
    CHECK(l.effect[0] == '\0' && l.color[0] == '\0' && l.brightness == 9);
    write_file(file, "[unmuted]\neffect = 5\nbrightness = 300\n");
    CHECK(is_empty(peri_look_load(ID, UN)));
    write_file(file, "[unmuted]\neffect = \"breathing\"\ncolor2 = \"ff0000\"\n"); // second without a first
    l = peri_look_load(ID, UN);
    CHECK(strcmp(l.effect, "breathing") == 0 && l.color[0] == '\0' && l.color2[0] == '\0');
    write_file(file, "[muted]\neffect = \"spectrum\"\ncolor = \"0000ff\"\nbrightness = \"high\"\n");
    l = peri_look_load(ID, MU); // a stray colour and a non-number are ignored
    CHECK(strcmp(l.effect, "spectrum") == 0 && l.color[0] == '\0' && l.brightness == -1);
    CHECK(is_empty(peri_look_load(ID, UN)));

    // A symlink is not followed, and an invalid id never touches the disk.
    unlink(file);
    CHECK(symlink("/etc/hostname", file) == 0);
    CHECK(is_empty(peri_look_load(ID, UN)));
    CHECK(is_empty(peri_look_load("../x", UN)));

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
    CHECK(peri_look_save(ID, MU, &good));
    CHECK(strcmp(peri_look_load(ID, MU).effect, "spectrum") == 0);

    nftw(root, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
    nftw(home, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
    return failures ? 1 : 0;
}
