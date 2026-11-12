// Offline host test for the native front end (src/LuckyDipPlugin.cpp) with the real engine, under ASan/UBSan (tests/run.sh).
// A fake VST2 host drives it against a small sample library made here (WAV files in category folders):
// identity and the frozen order, the library scan, GENERATE, pad notes -> sound (44.1 and 48 kHz), value / toggle / momentary /
// step / readout params, the parameter-0 nudge, a state round trip into a second instance, a 0.9.2 project chunk
// (tests/data/legacy-0.9.2.chunk, made by the released engine itself), and bad chunks.
// Prints OK at the end, or the failed checks and exits 1.
// Built with -DMKLIB_ONLY it is only the library maker tests/make_legacy_chunk.sh uses.
#include <sys/stat.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void putLe(std::string& s, uint32_t v, int n) { for (int i = 0; i < n; i++) s += (char)((v >> (8 * i)) & 255); }
static std::string wav(int amp, int frames, int rate)
{
    std::string d;
    for (int i = 0; i < frames; i++) putLe(d, (uint16_t)(int16_t)((i % 40) < 20 ? amp : -amp), 2);
    std::string f = "RIFF"; putLe(f, 36 + (uint32_t)d.size(), 4); f += "WAVEfmt "; putLe(f, 16, 4); putLe(f, 1, 2); putLe(f, 1, 2);
    putLe(f, (uint32_t)rate, 4); putLe(f, (uint32_t)rate * 2, 4); putLe(f, 2, 2); putLe(f, 16, 2); f += "data"; putLe(f, (uint32_t)d.size(), 4);
    return f + d;
}
// The same files every time (the legacy chunk names them by path).
static void makeLib(const std::string& root)
{
    static const char* const dirs[] = {"Kicks", "Snares", "Rims", "Claps", "Hats", "Closed Hats", "Open Hats", "Toms", "Congas",
                                       "Percussion", "Crashes", "Rides", "Cymbals", "FX", "Vox", "Bass", "Synth", "Chords"};
    mkdir(root.c_str(), 0755);
    int k = 0;
    for (const char* d : dirs)
    {
        const std::string dir = root + "/" + d;
        mkdir(dir.c_str(), 0755);
        for (int i = 1; i <= 3; i++, k++)
        {
            char nm[64];
            std::snprintf(nm, sizeof nm, "/hit_%02d.wav", i);
            FILE* f = std::fopen((dir + nm).c_str(), "wb");
            if (!f) continue;
            const std::string w = wav(3000 + 400 * (k % 20), 8000, i == 3 ? 48000 : 44100);
            std::fwrite(w.data(), 1, w.size(), f);
            std::fclose(f);
        }
    }
}

#ifdef MKLIB_ONLY
int main(int argc, char** argv) { if (argc < 2) return 2; makeLib(argv[1]); return 0; }
#else

#include "mpc_vst2_abi.h"
#include "params_gen.h"

#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

extern "C" AEffect* VSTPluginMain(audioMasterCallback);
extern "C" void ld_dbg_tick(AEffect*);
extern "C" int ld_dbg_engine(AEffect*, const char*, char*, int);
extern "C" void ld_dbg_engine_set(AEffect*, const char*, const char*);

static std::atomic<int> g_automate[kNumParams];
static std::atomic<int> g_updateDisplay{0};
static int g_fail = 0;
static VstTimeInfo g_time;

static VstIntPtr host(AEffect*, VstInt32 op, VstInt32 idx, VstIntPtr, void*, float)
{
    if (op == audioMasterAutomate && idx >= 0 && idx < kNumParams) g_automate[idx]++;
    if (op == audioMasterVersion) return 2400;
    if (op == audioMasterUpdateDisplay) g_updateDisplay++;
    if (op == audioMasterGetTime) return (VstIntPtr)&g_time;
    return 0;
}

#define CHECK(c, ...) do { if (c) { std::printf("  ok   "); } else { std::printf("  FAIL "); g_fail++; } std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

static int idx(const char* key)
{
    for (int i = 0; i < kNumParams; i++) if (!std::strcmp(g_pspecs[i].key, key)) return i;
    std::printf("no param %s\n", key); std::exit(2);
}
static float norm(int i, float plain) { const PSpec& s = g_pspecs[i]; return s.max > s.min ? (plain - s.min) / (s.max - s.min) : 0.f; }
static float plainOf(AEffect* e, int i) { const PSpec& s = g_pspecs[i]; return s.min + e->getParameter(e, i) * (s.max - s.min); }
static std::string eng(AEffect* e, const char* k) { static char b[8192]; ld_dbg_engine(e, k, b, sizeof b); return b; }
static int engi(AEffect* e, const char* k) { return std::atoi(eng(e, k).c_str()); }
static std::string disp(AEffect* e, int i) { char b[128] = {0}; e->dispatcher(e, effGetParamDisplay, i, 0, b, 0); return b; }
static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static void set(AEffect* e, const char* k, float plain) { e->setParameter(e, idx(k), norm(idx(k), plain)); }
static void press(AEffect* e, const char* k) { set(e, k, 1); ld_dbg_tick(e); set(e, k, 0); ld_dbg_tick(e); }

static bool waitFor(AEffect* e, const char* key, const char* needle, int ms = 8000)
{
    for (int t = 0; t < ms; t += 20) { if (eng(e, key).find(needle) != std::string::npos) return true; sleepMs(20); }
    return false;
}

static double g_peak;
static void run(AEffect* e, int blocks, int note = -1)
{
    float L[128], R[128], *out[2] = {L, R};
    g_peak = 0;
    for (int b = 0; b < blocks; b++)
    {
        if (b == 0 && note >= 0)
        {
            VstMidiEvent ev;
            std::memset(&ev, 0, sizeof ev);
            ev.type = kVstMidiType; ev.byteSize = sizeof(VstMidiEvent); ev.deltaFrames = 17;
            ev.midiData[0] = (char)0x90; ev.midiData[1] = (char)note; ev.midiData[2] = 120;
            std::vector<char> buf(sizeof(VstEvents) + sizeof(VstEvent*));
            VstEvents* evs = reinterpret_cast<VstEvents*>(buf.data());
            evs->numEvents = 1; evs->events[0] = reinterpret_cast<VstEvent*>(&ev);
            e->dispatcher(e, effProcessEvents, 0, 0, evs, 0);
        }
        e->processReplacing(e, nullptr, out, 128);
        for (int i = 0; i < 128; i++) { g_peak = std::max(g_peak, (double)std::fabs(L[i])); g_peak = std::max(g_peak, (double)std::fabs(R[i])); }
    }
}

static AEffect* open(double rate = 44100)
{
    AEffect* e = VSTPluginMain(host);
    if (!e) { std::printf("no effect\n"); std::exit(2); }
    e->dispatcher(e, effOpen, 0, 0, nullptr, 0);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, (float)rate);
    e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0);
    return e;
}
static void close(AEffect* e)
{
    e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0);
    e->dispatcher(e, effClose, 0, 0, nullptr, 0);
}
static std::vector<char> chunk(AEffect* e)
{
    void* p = nullptr;
    const VstIntPtr n = e->dispatcher(e, effGetChunk, 0, 0, &p, 0);
    return n > 0 && p ? std::vector<char>((char*)p, (char*)p + n) : std::vector<char>();
}

int main(int argc, char** argv)
{
    const char* legacy = argc > 1 ? argv[1] : nullptr;
    const char* pf = std::getenv("MPC_PLUGIN_FOLDER");
    if (!pf) { std::printf("set MPC_PLUGIN_FOLDER\n"); return 2; }
    const std::string lib = "/tmp/luckydip-legacy-lib";   // the legacy chunk's paths
    makeLib(lib);
    const std::string data = std::string(pf) + "/luckydip";
    mkdir(data.c_str(), 0755);
    { std::ofstream c(data + "/luckydip.conf"); c << "root=" << lib << "\n"; }

    std::printf("identity and order\n");
    AEffect* e = open();
    {
        char b[256] = {0};
        e->dispatcher(e, effGetEffectName, 0, 0, b, 0);
        CHECK(std::string(b) == LD_EXPECT_NAME, "name \"%s\"", b);
        const char want[] = LD_EXPECT_UID;
        CHECK(e->uniqueID == (VstInt32)(((unsigned)want[0] << 24) | ((unsigned)want[1] << 16) | ((unsigned)want[2] << 8) | (unsigned)want[3]),
              "uid %08x", (unsigned)e->uniqueID);
        CHECK(e->numParams == kNumParams && kNumParams == 178, "%d params", e->numParams);
        std::ifstream lk("vst/params.lock");
        std::string k;
        int i = 0, bad = 0;
        while (lk >> k) { if (i >= kNumParams || k != g_pspecs[i].key) bad++; i++; }
        CHECK(i == 161 && !bad, "params.lock order (%d keys, %d differ)", i, bad);
        CHECK(e->flags & effFlagsIsSynth, "instrument");
    }

    std::printf("library scan and GENERATE\n");
    CHECK(waitFor(e, "lib_info", " samples"), "scan: %s", eng(e, "lib_info").c_str());
    CHECK(eng(e, "lib_info") == "54 samples", "all 54 files found (%s)", eng(e, "lib_info").c_str());
    press(e, "generate");
    CHECK(eng(e, "pad1_name") != "(empty)" && eng(e, "pad16_name") != "(empty)", "pads filled: %s / %s", eng(e, "pad1_name").c_str(), eng(e, "pad16_name").c_str());
    CHECK(plainOf(e, idx("generate")) == 0, "GENERATE dropped back to 0");
    CHECK(eng(e, "pad1_cat") == "kick", "pad 1 drew a kick (%s)", eng(e, "pad1_cat").c_str());
    ld_dbg_tick(e);
    CHECK(std::lround(plainOf(e, idx("pad1_kind"))) == 1 && std::lround(plainOf(e, idx("pad2_kind"))) == 2, "pad kinds: kick = 1, snare = 2");
    CHECK(std::lround(plainOf(e, idx("sel_kind"))) == std::lround(plainOf(e, idx("pad1_kind"))), "selected pad kind");
    {
        const int before = (int)plainOf(e, idx("pad1_name"));
        ld_dbg_tick(e);
        CHECK(disp(e, idx("pad1_name")) == eng(e, "pad1_name"), "pad 1 readout text \"%s\"", disp(e, idx("pad1_name")).c_str());
        press(e, "pad1_reroll");
        ld_dbg_tick(e);
        (void)before;
        CHECK(!disp(e, idx("status")).empty(), "status readout \"%s\"", disp(e, idx("status")).c_str());
    }

    std::printf("sound\n");
    sleepMs(300);   // the worker decodes the samples
    run(e, 40, 36);
    CHECK(g_peak > 0.02, "note 36 plays pad 1 (peak %.3f)", g_peak);
    run(e, 200);
    run(e, 40, 1);
    CHECK(g_peak > 0.02, "note 1 plays pad 2 (peak %.3f)", g_peak);

    std::printf("values, toggles, steps\n");
    set(e, "pad2_gain", 150); ld_dbg_tick(e);
    CHECK(engi(e, "pad2_gain") == 150, "pad 2 gain -> engine (%d)", engi(e, "pad2_gain"));
    set(e, "pad1_lock", 1); ld_dbg_tick(e);
    CHECK(eng(e, "pad1_lock_on") == "1" && plainOf(e, idx("pad1_lock")) == 1, "pad 1 lock on");
    CHECK(disp(e, idx("pad1_lock")) == "LOCK", "lock label \"%s\"", disp(e, idx("pad1_lock")).c_str());
    const std::string locked = eng(e, "pad1_name");
    press(e, "generate");
    CHECK(eng(e, "pad1_name") == locked, "a locked pad keeps its sample");
    set(e, "follow", 0); ld_dbg_tick(e);
    set(e, "sel_pad", 1); ld_dbg_tick(e);
    press(e, "sel_next"); ld_dbg_tick(e);
    CHECK(engi(e, "sel_pad") == 2 && (int)std::lround(plainOf(e, idx("sel_pad"))) == 2, "PAD > selects pad 2");
    ld_dbg_tick(e);
    CHECK(std::lround(plainOf(e, idx("sel_gain"))) == 150, "SEL GAIN follows the selection (%ld)", std::lround(plainOf(e, idx("sel_gain"))));
    press(e, "sel_prev"); press(e, "sel_prev"); ld_dbg_tick(e);
    CHECK(engi(e, "sel_pad") == 1, "PAD < stops at pad 1");
    set(e, "sel_cat_bass", 1); ld_dbg_tick(e);
    CHECK(eng(e, "sel_cat_bass_on") == "1" && plainOf(e, idx("sel_cat_bass")) == 1, "pad 1 may draw bass");
    CHECK(eng(e, "sel_pill") == "KICK+" || eng(e, "pad1_pill").find('+') != std::string::npos, "pool pill shows the override (%s)", eng(e, "pad1_pill").c_str());
    // Follow: a played pad becomes the selection, and the host is told
    set(e, "follow", 1); ld_dbg_tick(e);
    g_automate[idx("sel_pad")] = 0;
    run(e, 4, 36 + 6);
    ld_dbg_tick(e);
    CHECK(engi(e, "sel_pad") == 7 && std::lround(plainOf(e, idx("sel_pad"))) == 7 && g_automate[idx("sel_pad")] > 0, "FOLLOW: playing pad 7 selects it, host told");
    // parameter 0 (the status readout): the host's nudge must not do anything
    const std::string st0 = eng(e, "state");
    e->setParameter(e, 0, 1.0f); e->setParameter(e, 0, 0.0f); ld_dbg_tick(e);
    CHECK(eng(e, "state") == st0, "parameter-0 nudge is harmless");
    // random sweep: nothing crashes, momentaries end at 0
    srand(7);
    for (int r = 0; r < 600; r++)
    {
        const int i = rand() % kNumParams;
        const PSpec& s = g_pspecs[i];
        if (!std::strcmp(s.key, "export") || !std::strcmp(s.key, "rescan") || std::strstr(s.key, "_reject") || std::strstr(s.key, "_fav")) continue;
        e->setParameter(e, i, (float)(rand() % 1000) / 999.0f);
        if (r % 7 == 0) ld_dbg_tick(e);
        if (r % 50 == 0) run(e, 2);
    }
    for (int i = 0; i < kNumParams; i++) if (g_pspecs[i].flags & kMomentary) e->setParameter(e, i, 0);
    ld_dbg_tick(e); ld_dbg_tick(e);
    {
        int stuck = 0;
        for (int i = 0; i < kNumParams; i++) if ((g_pspecs[i].flags & kMomentary) && plainOf(e, i) != 0) stuck++;
        CHECK(!stuck, "random sweep: no momentary left high (%d)", stuck);
    }

    std::printf("state round trip\n");
    set(e, "pad1_lock", 1); set(e, "pad3_gain", 42); set(e, "prevent_dup", 0); ld_dbg_tick(e);
    sleepMs(50); ld_dbg_tick(e);
    const std::string want = eng(e, "state");
    std::vector<char> c = chunk(e);
    CHECK(c.size() > 8 && !std::memcmp(c.data(), "MPCP", 4), "MPC chunk (%zu bytes)", c.size());
    AEffect* e2 = open();
    e2->dispatcher(e2, effSetChunk, 0, (VstIntPtr)c.size(), c.data(), 0);
    ld_dbg_tick(e2);
    CHECK(eng(e2, "state") == want, "second instance has the same kit");
    CHECK(plainOf(e2, idx("pad1_lock")) == 1 && std::lround(plainOf(e2, idx("pad3_gain"))) == 42 && plainOf(e2, idx("prevent_dup")) == 0,
          "host values follow the state");
    {
        std::vector<char> c2 = chunk(e2);
        AEffect* e3 = open();
        e3->dispatcher(e3, effSetChunk, 0, (VstIntPtr)c2.size(), c2.data(), 0);
        ld_dbg_tick(e3);
        CHECK(eng(e3, "state") == want, "and again (a project saved by the new build, reloaded)");
        close(e3);
    }
    close(e2);

    std::printf("0.9.2 project\n");
    if (legacy)
    {
        std::ifstream f(legacy, std::ios::binary);
        std::vector<char> lc((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(lc.size() > 4 && !std::memcmp(lc.data(), "LD1\n", 4), "legacy chunk read (%zu bytes)", lc.size());
        AEffect* e4 = open();
        const VstIntPtr r = e4->dispatcher(e4, effSetChunk, 0, (VstIntPtr)lc.size(), lc.data(), 0);
        ld_dbg_tick(e4);
        CHECK(r == 1, "effSetChunk accepted it");
        const std::string st(lc.data(), strnlen(lc.data(), lc.size()));
        CHECK(eng(e4, "state") == st, "engine state equals the 0.9.2 save");
        CHECK(plainOf(e4, idx("pad1_lock")) == 1 && std::lround(plainOf(e4, idx("pad2_gain"))) == 150 && plainOf(e4, idx("follow")) == 0 &&
              std::lround(plainOf(e4, idx("sel_pad"))) == 3 && plainOf(e4, idx("sel_cat_bass")) == 1 && std::lround(plainOf(e4, idx("max_size"))) == 2,
              "host values: lock, gain, follow, pad, category, max size");
        CHECK(eng(e4, "pad1_name") != "(empty)" && eng(e4, "pad1_name")[0] != '!', "pad 1 sample found (%s)", eng(e4, "pad1_name").c_str());
        sleepMs(300);
        run(e4, 40, 36);
        CHECK(g_peak > 0.02, "pad 1 of the old project plays (peak %.3f)", g_peak);
        close(e4);
    }
    else CHECK(false, "no legacy chunk given");

    std::printf("48 kHz host\n");
    {
        AEffect* e5 = open(48000);
        e5->dispatcher(e5, effSetChunk, 0, (VstIntPtr)c.size(), c.data(), 0);
        ld_dbg_tick(e5);
        sleepMs(300);
        run(e5, 40, 36);
        CHECK(g_peak > 0.02, "plays at 48 kHz (peak %.3f)", g_peak);
        // the test square wave has a 40-frame period at 44.1 kHz: at 48 kHz it must last 40 * 48000 / 44100 = 43.5 frames
        float L[1024], R[1024], *out[2] = {L, R};
        AEffect* e6 = open(48000);
        e6->dispatcher(e6, effSetChunk, 0, (VstIntPtr)c.size(), c.data(), 0);
        ld_dbg_tick(e6); sleepMs(300);
        run(e6, 1, 36);
        e6->processReplacing(e6, nullptr, out, 1024);
        int cross = 0;
        for (int i = 1; i < 1024; i++) if ((L[i - 1] < 0) != (L[i] < 0)) cross++;
        const double period = cross ? 2.0 * 1023 / cross : 0;
        // hit_03 files are 48 kHz sources (40 frames at 48 kHz), the others 44.1 kHz (43.5 frames at 48 kHz)
        const double wantP = eng(e6, "pad1_name").find("hit_03") != std::string::npos ? 40.0 : 43.5;
        CHECK(std::fabs(period - wantP) < 1.5, "pitch kept at 48 kHz (period %.1f frames, want %.1f)", period, wantP);
        close(e6);
        close(e5);
    }

    std::printf("bad chunks\n");
    {
        const char junk[] = "garbage that is not a chunk";
        CHECK(e->dispatcher(e, effSetChunk, 0, sizeof junk, (void*)junk, 0) == 0, "junk refused");
        const char ld[] = "LD1\npad\t99\t1\t0\t0\t1.0\t/nope\n";
        e->dispatcher(e, effSetChunk, 0, sizeof ld, (void*)ld, 0);
        ld_dbg_tick(e);
        CHECK(true, "out-of-range pad line survived");
    }
    close(e);
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
#endif
