/* Lucky Dip engine test (x86, ASan): the real mpc_engine() driven the way the wrapper drives it, against a scratch
 * sample library: scan -> Generate -> decode -> pad notes sound -> state round trip -> reroll/lock/clear ->
 * Match Levels -> Export. Build via vst/test.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <chrono>
extern "C" {
#include "engine.h"
}
#include "ld_core.h"
#include "ld_xpm.h"
static int fails;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static void put_le(std::string &s, uint32_t v, int n) { for (int i = 0; i < n; i++) s += (char)((v >> (8 * i)) & 255); }
static std::string wav(int amp, int frames) {
    std::string d;
    for (int i = 0; i < frames; i++) put_le(d, (uint16_t)(int16_t)((i % 40) < 20 ? amp : -amp), 2);
    std::string f = "RIFF"; put_le(f, 36 + d.size(), 4); f += "WAVEfmt "; put_le(f, 16, 4); put_le(f, 1, 2); put_le(f, 1, 2);
    put_le(f, 44100, 4); put_le(f, 88200, 4); put_le(f, 2, 2); put_le(f, 16, 2); f += "data"; put_le(f, d.size(), 4); return f + d;
}
static const mpc_engine_t *E;
static std::string get(void *h, const char *k) { char b[8192] = ""; E->get_param(h, k, b, sizeof b); return b; }
static void trig(void *h, const char *k) { E->set_param(h, k, "1.000000"); }
static bool wait_for(void *h, const char *key, const char *prefix, int ms = 5000) {
    for (int t = 0; t < ms; t += 20) { if (get(h, key).compare(0, strlen(prefix), prefix) == 0) return true; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    return false;
}
static int peak(void *h, int blocks) {
    int pk = 0; int16_t out[256];
    for (int b = 0; b < blocks; b++) { E->render(h, out, 128); for (int i = 0; i < 256; i++) pk = std::max(pk, abs((int)out[i])); }
    return pk;
}

int main() {
    E = mpc_engine();
    char tmpl[] = "/tmp/ldengXXXXXX";
    std::string root = mkdtemp(tmpl), lib = root + "/lib", data = root + "/data";
    const char *dirs[] = {"Kicks", "Snares", "Claps", "Fx", "Synth"};
    for (size_t d = 0; d < 5; d++)
        for (int k = 1; k <= 3; k++) {
            ld::make_dirs(lib + "/" + dirs[d]);
            ld::write_text(lib + "/" + dirs[d] + "/s" + std::to_string(k) + ".wav", wav(d == 0 ? 20000 : 5000, 8000));
        }
    ld::make_dirs(data);
    ld::write_text(data + "/luckydip.conf", "root=" + lib + "\nexport_dir=" + root + "/kits\n");

    void *h = E->create(data.c_str());
    CHECK(h, "create");
    CHECK(wait_for(h, "status", "Library: 15"), "scan finished: \"%s\"", get(h, "status").c_str());
    CHECK(get(h, "pad1_name") == "(empty)", "pads start empty");
    CHECK(peak(h, 4) == 0, "silent with no kit");

    trig(h, "generate");
    CHECK(get(h, "status").compare(0, 3, "Kit") == 0, "generate: \"%s\"", get(h, "status").c_str());
    CHECK(get(h, "pad1_name") != "(empty)" && get(h, "pad1_pill") == "KICK", "pad 1 = %s (%s)", get(h, "pad1_name").c_str(), get(h, "pad1_pill").c_str());
    int filled = 0; for (int i = 1; i <= 16; i++) filled += get(h, ("pad" + std::to_string(i) + "_name").c_str()) != "(empty)";
    CHECK(filled > 0, "%d pads assigned", filled);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));       /* decode */
    uint8_t on[3] = {0x90, 0, 127};
    E->midi(h, on, 3);
    int pk = peak(h, 8);
    CHECK(pk > 10000, "note 0 (drum-pad layout) plays pad 1, peak %d", pk);
    peak(h, 100);      /* let the 8000-frame one-shot finish */
    CHECK(peak(h, 4) == 0, "one-shot ends");
    on[1] = 36; E->midi(h, on, 3);
    CHECK(peak(h, 8) > 10000, "note 36 plays pad 1 too");
    peak(h, 100);
    on[1] = 20; E->midi(h, on, 3);
    CHECK(peak(h, 8) == 0, "note 20 is not a pad");

    std::string st = get(h, "state");
    void *h2 = E->create(data.c_str());
    E->set_param(h2, "state", st.c_str());
    CHECK(get(h2, "pad1_name") == get(h, "pad1_name"), "state restores the kit on a second instance");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    E->midi(h2, (uint8_t[]){0x90, 0, 127}, 3);
    CHECK(peak(h2, 8) > 10000, "restored kit plays");

    std::string before = get(h, "pad1_name");
    E->set_param(h, "pad1_lock", "1");
    trig(h, "generate");
    CHECK(get(h, "pad1_name") == before && get(h, "pad1_pill") == "L:KICK", "locked pad survives Generate (%s)", get(h, "pad1_pill").c_str());
    trig(h, "pad1_reroll");
    CHECK(get(h, "status").find("locked") != std::string::npos, "reroll on a locked pad refuses: \"%s\"", get(h, "status").c_str());
    E->set_param(h, "pad1_lock", "0");
    E->set_param(h, "sel_pad", "2");
    CHECK(get(h, "sel_pad") == "2" && get(h, "sel_name") == get(h, "pad2_name"), "sel_* follows the selected pad");
    E->set_param(h, "sel_cat_fx", "1");
    CHECK(get(h, "pad2_pill") == "FX" && get(h, "sel_cat_fx") == "1", "per-pad pool override: %s", get(h, "pad2_pill").c_str());
    E->set_param(h, "sel_cat_synth", "1");
    CHECK(get(h, "pad2_pill") == "FX+", "two categories: %s", get(h, "pad2_pill").c_str());
    E->set_param(h, "sel_cat_synth", "0");
    trig(h, "sel_reroll");
    CHECK(get(h, "sel_cat") == "fx", "pad 2 rerolled from the fx pool (%s)", get(h, "sel_cat").c_str());
    trig(h, "sel_clear");
    CHECK(get(h, "pad2_name") == "(empty)", "clear");

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    trig(h, "normalise");
    CHECK(get(h, "status") == "Levels matched", "match levels");
    int g1 = atoi(get(h, "pad1_gain").c_str());
    CHECK(g1 < 100 && g1 >= 15, "loud kick turned down toward the quiet pads: %d%%", g1);

    trig(h, "export");
    CHECK(wait_for(h, "status", "Exported"), "export: \"%s\"", get(h, "status").c_str());
    CHECK(get(h, "export_name").compare(0, 8, "LuckyDip") == 0, "export name %s", get(h, "export_name").c_str());
    std::vector<uint8_t> xb;
    CHECK(ld::read_file(root + "/kits/" + get(h, "export_name") + "/" + get(h, "export_name") + ".xpm", xb), "xpm written");

    /* settings: a saved source/export folder is applied, a scan follows, exports skip nothing but land where chosen */
    CHECK(get(h, "src_name") == "Default (auto)" && get(h, "lib_info") == "15 samples", "default source: %s / %s", get(h, "src_name").c_str(), get(h, "lib_info").c_str());
    std::string st2 = get(h, "state");
    CHECK(st2.find("\nsrc=\n") != std::string::npos, "state records the default source");
    void *h3 = E->create(data.c_str());
    E->set_param(h3, "state", ("LD1\nsrc=" + lib + "/Kicks\nexp=" + root + "/chosen\n").c_str());
    CHECK(wait_for(h3, "status", "Library: 3"), "saved source triggers a scan of just that folder: \"%s\"", get(h3, "status").c_str());
    CHECK(get(h3, "lib_info") == "3 samples", "lib_info %s", get(h3, "lib_info").c_str());
    CHECK(get(h3, "src_name").find("Kicks") != std::string::npos && get(h3, "exp_name").find("chosen") != std::string::npos, "labels: %s | %s", get(h3, "src_name").c_str(), get(h3, "exp_name").c_str());
    trig(h3, "generate");
    int k3 = 0; for (int i = 1; i <= 16; i++) k3 += get(h3, ("pad" + std::to_string(i) + "_name").c_str()) != "(empty)";
    CHECK(k3 > 0 && get(h3, "pad1_pill") == "KICK" && get(h3, "pad2_name") == "(empty)", "only kicks to draw from: %d pads, pad1 %s/%s, pad2 %s", k3, get(h3, "pad1_pill").c_str(), get(h3, "pad1_name").c_str(), get(h3, "pad2_name").c_str());
    trig(h3, "export");
    CHECK(wait_for(h3, "status", "Exported"), "export: \"%s\"", get(h3, "status").c_str());
    std::vector<uint8_t> xb3;
    CHECK(ld::read_file(root + "/chosen/" + get(h3, "export_name") + "/" + get(h3, "export_name") + ".xpm", xb3), "xpm landed in the chosen export folder");
    trig(h3, "src_next");
    CHECK(get(h3, "status").compare(0, 6, "Source") == 0, "stepping the source says to rescan: \"%s\"", get(h3, "status").c_str());
    E->destroy(h3);
    E->destroy(h2);
    E->destroy(h);      /* joins the worker and scanner threads; ASan checks nothing leaks or races a freed buffer */
    std::string cmd = "rm -rf " + root; if (system(cmd.c_str())) {}
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails ? 1 : 0;
}
