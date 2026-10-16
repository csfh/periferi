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

    // The request's look rules, with no file and no device. A bad value is named
    // in the order it was written; what would be stored drops what the effect
    // does not take.
    struct peri_look got;
    CHECK(peri_look_read(nullptr, 0, &got) == nullptr && got.brightness == -1 && got.effect[0] == '\0');
    struct peri_look_field static_blue[] = {
        { "effect", "static" }, { "color", "0000FF" }, { "brightness", "102" },
    };
    CHECK(peri_look_read(static_blue, 3, &got) == nullptr);
    const char *why = peri_look_stored(&got, &got);
    CHECK(why == nullptr && strcmp(got.effect, "static") == 0 && strcmp(got.color, "0000ff") == 0
          && got.brightness == 102 && got.color2[0] == '\0');

    struct peri_look_field bad_first[] = { { "speed", "warp" }, { "color", "zz" } };
    CHECK(strcmp(peri_look_read(bad_first, 2, &got), "speed must be slow or fast") == 0);
    struct peri_look_field bad_color_first[] = { { "color", "zz" }, { "speed", "warp" } };
    CHECK(strcmp(peri_look_read(bad_color_first, 2, &got), "color must be six hex digits, like 0000ff") == 0);
    struct peri_look_field unknown_effect[] = { { "effect", "rainbow" } };
    CHECK(strcmp(peri_look_read(unknown_effect, 1, &got),
                 "effect must be none, static, breathing, spectrum, wave or wave-reverse") == 0);
    struct peri_look_field bad_bright[] = { { "brightness", "256" } };
    CHECK(strcmp(peri_look_read(bad_bright, 1, &got), "brightness must be a whole number from 0 to 255") == 0);
    struct peri_look_field bad_c2[] = { { "color2", "12345" } };
    CHECK(strcmp(peri_look_read(bad_c2, 1, &got), "color2 must be six hex digits, like ff0000") == 0);
    CHECK(strcmp(peri_look_read(nullptr, 1, &got), "not a look setting") == 0);

    struct peri_look_field just_static[] = { { "effect", "static" } };
    CHECK(peri_look_read(just_static, 1, &got) == nullptr);
    CHECK(strcmp(peri_look_stored(&got, &got), "the static effect needs a color=<rrggbb>") == 0);
    struct peri_look_field second_only[] = { { "effect", "breathing" }, { "color2", "ff0000" } };
    CHECK(peri_look_read(second_only, 2, &got) == nullptr);
    CHECK(strcmp(peri_look_stored(&got, &got), "a second colour needs a first") == 0);

    struct peri_look_field wave[] = { { "effect", "wave-reverse" }, { "speed", "fast" }, { "color", "aabbcc" } };
    CHECK(peri_look_read(wave, 3, &got) == nullptr && peri_look_stored(&got, &got) == nullptr);
    CHECK(strcmp(got.effect, "wave-reverse") == 0 && strcmp(got.speed, "fast") == 0 && got.color[0] == '\0');
    struct peri_look_field spectrum_speed[] = { { "effect", "spectrum" }, { "speed", "slow" } };
    CHECK(peri_look_read(spectrum_speed, 2, &got) == nullptr && peri_look_stored(&got, &got) == nullptr);
    CHECK(strcmp(got.effect, "spectrum") == 0 && got.speed[0] == '\0');
    struct peri_look_field two[] = {
        { "effect", "breathing" }, { "color", "00FF00" }, { "color2", "FF0000" },
    };
    CHECK(peri_look_read(two, 3, &got) == nullptr && peri_look_stored(&got, &got) == nullptr);
    CHECK(strcmp(got.color, "00ff00") == 0 && strcmp(got.color2, "ff0000") == 0);
    CHECK(peri_look_stored(nullptr, &got) != nullptr);

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

    // Breathing takes no colour, one, or two; a second needs a first.
    struct peri_look b2 = look("breathing", "00FF00", -1);
    snprintf(b2.color2, sizeof b2.color2, "%s", "FF0000");
    CHECK(peri_look_save(ID, UN, &b2));
    l = peri_look_load(ID, UN);
    CHECK(strcmp(l.effect, "breathing") == 0 && strcmp(l.color, "00ff00") == 0
          && strcmp(l.color2, "ff0000") == 0);
    struct peri_look s1 = look("breathing", "", -1);
    snprintf(s1.color2, sizeof s1.color2, "%s", "ff0000");      // no first colour: the second is dropped
    CHECK(peri_look_save(ID, UN, &s1));
    l = peri_look_load(ID, UN);
    CHECK(strcmp(l.effect, "breathing") == 0 && l.color[0] == '\0' && l.color2[0] == '\0');
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
    const char *names[] = { "wave", "wave-reverse" };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        struct peri_look n = look(names[i], "", -1);
        CHECK(peri_look_save(ID, MU, &n));
        CHECK(strcmp(peri_look_load(ID, MU).effect, names[i]) == 0);
    }
    struct peri_look nope = look("wheel", "", -1);   // not an effect this mic shows
    CHECK(!peri_look_save(ID, MU, &nope));

    // A wave may be slow or fast; no other effect keeps a speed, and a bad speed is refused.
    struct peri_look fast_wave = look("wave", "", -1);
    snprintf(fast_wave.speed, sizeof fast_wave.speed, "%s", "fast");
    CHECK(peri_look_save(ID, UN, &fast_wave));
    CHECK(strcmp(peri_look_load(ID, UN).speed, "fast") == 0);
    struct peri_look wr = look("wave-reverse", "", 40);
    snprintf(wr.speed, sizeof wr.speed, "%s", "slow");
    CHECK(peri_look_save(ID, MU, &wr));
    CHECK(strcmp(peri_look_load(ID, MU).speed, "slow") == 0 && strcmp(peri_look_load(ID, MU).effect, "wave-reverse") == 0);
    CHECK(strcmp(peri_look_load(ID, UN).speed, "fast") == 0);           // the other look is untouched
    struct peri_look sp2 = look("spectrum", "", -1);
    snprintf(sp2.speed, sizeof sp2.speed, "%s", "fast");               // spectrum has no speed
    CHECK(peri_look_save(ID, UN, &sp2));
    CHECK(peri_look_load(ID, UN).speed[0] == '\0');
    struct peri_look bad_speed = look("wave", "", -1);
    snprintf(bad_speed.speed, sizeof bad_speed.speed, "%s", "warp");
    CHECK(!peri_look_save(ID, UN, &bad_speed));
    CHECK(strcmp(peri_look_load(ID, UN).effect, "spectrum") == 0);      // the refused save changed nothing
    write_file(file, "[unmuted]\neffect = \"wave\"\nspeed = \"warp\"\nbrightness = 7\n");
    l = peri_look_load(ID, UN);                                        // a bad speed spoils only the look
    CHECK(l.effect[0] == '\0' && l.speed[0] == '\0' && l.brightness == 7);
    write_file(file, "[unmuted]\neffect = \"spectrum\"\nspeed = \"fast\"\n");
    CHECK(peri_look_load(ID, UN).speed[0] == '\0');                    // a stray speed is ignored

    // Following the mute state is on unless the file says otherwise, and no save changes it.
    CHECK(peri_look_follow(ID));
    CHECK(peri_look_follow("nothing-saved-for-this"));
    struct peri_look keep = look("breathing", "00ff00", 80);
    CHECK(peri_look_save(ID, UN, &keep));
    CHECK(peri_look_set_follow(ID, false));
    CHECK(!peri_look_follow(ID));
    l = peri_look_load(ID, UN);                                  // turning it off keeps the looks
    CHECK(strcmp(l.effect, "breathing") == 0 && l.brightness == 80);
    CHECK(peri_look_save(ID, MU, &keep));                        // and saving keeps it off
    CHECK(!peri_look_follow(ID));
    CHECK(strcmp(peri_look_load(ID, UN).effect, "breathing") == 0);
    CHECK(peri_look_set_follow(ID, true));
    CHECK(peri_look_follow(ID));
    FILE *sf = fopen(file, "r");                                 // back on: the line is gone again
    char whole[512] = "";
    if (sf) {
        size_t got = fread(whole, 1, sizeof whole - 1, sf);
        whole[got] = '\0';
        fclose(sf);
    }
    CHECK(strstr(whole, "follow_mute") == nullptr && strstr(whole, "[unmuted]") != nullptr);
    CHECK(!peri_look_set_follow("../evil", false) && !peri_look_set_follow(nullptr, false));
    CHECK(peri_look_follow("../evil"));                          // an invalid id reads as the default
    write_file(file, "follow_mute = false\n\n[muted]\neffect = \"spectrum\"\n");
    CHECK(!peri_look_follow(ID) && strcmp(peri_look_load(ID, MU).effect, "spectrum") == 0);
    write_file(file, "follow_mute = \"no\"\n");                  // not a boolean: the default
    CHECK(peri_look_follow(ID));
    CHECK(peri_look_set_follow(ID, true));

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
