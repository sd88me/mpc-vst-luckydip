/* Lucky Dip core test (x86): classifier, scanner, assignment, loudness, WAV decode, XPM export.
 *   g++ -std=gnu++11 -fsanitize=address,undefined -Isrc tests/core_test.cpp -o /tmp/core_test && /tmp/core_test */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ld_audio.h"
#include "ld_core.h"
#include "ld_xpm.h"
using namespace ld;
static int fails;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)

static void put_le(std::string &s, uint32_t v, int n) { for (int i = 0; i < n; i++) s += (char)((v >> (8 * i)) & 255); }
static std::string make_wav(int ch, int bits, int rate, int frames, bool extensible = false) {
    std::string d;
    for (int i = 0; i < frames; i++)
        for (int c = 0; c < ch; c++) {
            int v = (int)(8000.0 * ((i % 40) < 20 ? 1 : -1));     /* square wave, a known RMS */
            if (bits == 16) put_le(d, (uint16_t)v, 2); else if (bits == 24) put_le(d, (uint32_t)(v * 256) & 0xFFFFFF, 3); else put_le(d, 128 + v / 256, 1);
        }
    std::string f = "RIFF"; put_le(f, 4 + 8 + (extensible ? 40 : 16) + 8 + d.size(), 4); f += "WAVE";
    f += "fmt "; put_le(f, extensible ? 40 : 16, 4); put_le(f, extensible ? 0xFFFE : 1, 2); put_le(f, ch, 2); put_le(f, rate, 4);
    put_le(f, rate * ch * bits / 8, 4); put_le(f, ch * bits / 8, 2); put_le(f, bits, 2);
    if (extensible) { put_le(f, 22, 2); put_le(f, bits, 2); put_le(f, 3, 4); put_le(f, 1, 2); f += std::string("\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71", 14); }
    f += "data"; put_le(f, d.size(), 4); f += d;
    return f;
}
static void wr(const std::string &path, const std::string &data) { make_dirs(path.substr(0, path.find_last_of('/'))); write_text(path, data); }

int main() {
    Classifier cl;
    std::vector<std::string> v;
    /* the examples from sample_classifier.mjs's own doc comments */
    v = {"Percussion", "Closed Hat"};  CHECK(cl.classify(v, "") == C_CLOSED_HAT, "deepest folder wins: closed hat");
    v = {"One Shots"};                 CHECK(cl.classify(v, "punchy_kick_01.wav") == C_KICK, "filename fallback: kick");
    v = {"Kicks"};                     CHECK(cl.classify(v, "snare_layer.wav") == C_KICK, "folder beats filename");
    v = {"Textures"};                  CHECK(cl.classify(v, "") == C_PAD, "textures -> pad");
    v = {"Misc"};                      CHECK(cl.classify(v, "xyzzy.wav") == C_OTHER, "unmatched -> other");
    v = {"Hi-Hats"};                   CHECK(cl.classify(v, "Hihat Closed 01.wav") == C_CLOSED_HAT, "hat folder promoted by filename");
    v = {"Bass"};                      CHECK(cl.classify(v, "") == C_BASS && cl.classify_filename("bassline.wav") == C_OTHER, "bassline is not bass (token-exact)");
    CHECK(cl.classify_filename("DeepKick_01.wav") == C_KICK, "camelCase split");
    CHECK(cl.classify_filename("open hat 909.wav") == C_OPEN_HAT, "longest window: open hat beats hat");
    CHECK(cl.lookup("shaker") == C_HAT, "shaker is hat (first writer wins)");
    CHECK(looks_like_loop("Drum Loop 90.wav") && looks_like_loop("x [120].wav") && looks_like_loop("a 128 bpm.wav") && !looks_like_loop("Kick 01.wav"), "loop heuristic");

    /* matchGains: attenuate-only toward the 25th percentile */
    float rms[NPADS] = {0}, g[NPADS];
    rms[0] = 0.4f; rms[1] = 0.2f; rms[2] = 0.1f; rms[3] = 0.05f;
    match_gains(rms, g);
    CHECK(g[3] == 1.0f, "quietest keeps unity");
    CHECK(g[4] == 1.0f && g[0] >= 0.15f && g[0] < g[1] && g[1] < g[2], "louder pads are turned down, nothing boosted (%.3f %.3f %.3f)", g[0], g[1], g[2]);

    /* a scratch library */
    char tmpl[] = "/tmp/ldtestXXXXXX";
    std::string root = mkdtemp(tmpl);
    std::string wav = make_wav(1, 16, 44100, 4410);
    const char *files[] = {"Kicks/k1.wav", "Kicks/k2.wav", "Snares/s1.wav", "Snares/s2.wav", "Hats/Closed Hats/c1.wav", "LuckyDip-0101-000000/k1.wav", "Misc/Loop 120 bpm.wav", "Fx/f1.wav", "Synth/pad1.wav", "Claps/cl1.wav", ".hidden/x.wav", "Kicks/notes.txt"};
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) wr(root + "/" + files[i], wav);
    ScanOpts so; so.roots.push_back(root);
    Library lib; ScanStats st;
    scan_library(so, cl, lib.recs, st);
    lib.rebuild();
    CHECK(lib.recs.size() == 8, "scan found %zu samples (want 8: no loops, hidden or non-audio)", lib.recs.size());
    CHECK(st.loops == 1, "one loop skipped");
    CHECK(lib.by_cat[C_KICK].size() == 2 && lib.by_cat[C_CLOSED_HAT].size() == 1, "classified by folder");

    Pad pads[NPADS];
    AssignResult r = assign_kit(pads, lib, 1234);
    CHECK(pads[0].has && pads[0].sample.cat == C_KICK, "pad 1 is a kick");
    CHECK(pads[1].has && pads[1].sample.cat == C_SNARE, "pad 2 is a snare");
    std::set<std::string> uniq;
    for (int i = 0; i < NPADS; i++) if (pads[i].has) uniq.insert(pads[i].sample.path);
    int filled = 0; std::string empty;
    for (int i = 0; i < NPADS; i++) { filled += pads[i].has; if (!pads[i].has) empty += " " + std::to_string(i + 1); }
    /* this library has no open hat/hat/perc/tom/conga/ride/cymbal/crash: pads 7-11 have nothing to draw from */
    CHECK(r.unresolved == 5 && filled == 11, "%d pads filled, empty:%s (want 7-11 empty)", filled, empty.c_str());
    CHECK(r.relaxed > 0, "duplicates relaxed once a pool ran dry (relaxed %d)", r.relaxed);
    Pad again[NPADS];
    assign_kit(again, lib, 1234);
    bool same = true;
    for (int i = 0; i < NPADS; i++) same = same && again[i].sample.path == pads[i].sample.path;
    CHECK(same, "same seed, same kit");
    pads[0].locked = true;
    std::string keep = pads[0].sample.path;
    assign_kit(pads, lib, 99);
    CHECK(pads[0].sample.path == keep, "a locked pad survives Generate");
    lib.rejects.insert(lib.recs[lib.by_cat[C_KICK][0]].path);
    for (int s = 1; s < 20; s++) { Pad q[NPADS]; assign_kit(q, lib, s); CHECK(q[0].sample.path != lib.recs[lib.by_cat[C_KICK][0]].path, "seed %d: rejected sample never picked", s); if (fails) break; }

    /* WAV decode */
    Pcm pcm;
    CHECK(decode(root + "/Kicks/k1.wav", pcm) && pcm.frames() == 4410 && pcm.src_frames == 4410, "decode 16-bit mono 44.1k: %u frames", pcm.frames());
    CHECK(fabsf(pcm.rms - 8000.0f / 32768.0f) < 0.01f, "rms %.4f", pcm.rms);
    wr(root + "/w/a.wav", make_wav(2, 24, 48000, 4800));
    CHECK(decode(root + "/w/a.wav", pcm) && pcm.frames() == 4410 && pcm.src_frames == 4800, "decode 24-bit stereo 48k resampled to %u frames", pcm.frames());
    wr(root + "/w/b.wav", make_wav(2, 16, 44100, 1000, true));
    CHECK(decode(root + "/w/b.wav", pcm) && pcm.frames() == 1000, "decode WAVE_FORMAT_EXTENSIBLE");
    CHECK(!decode(root + "/w/none.wav", pcm), "missing file fails cleanly");
    wr(root + "/w/junk.wav", "RIFFxxxxWAVEjunk");
    CHECK(!decode(root + "/w/junk.wav", pcm), "junk file fails cleanly");
    CHECK(frame_count(root + "/Kicks/k1.wav") == 4410, "frame_count for SliceEnd");

    /* XPM export */
    pads[0].locked = false;
    ExportResult xr = export_xpm(root + "/out", "Test Kit", pads);
    CHECK(xr.ok && xr.pads == 11 && xr.gathered > 0, "export ok: %d pads, %d gathered", xr.pads, xr.gathered);
    std::vector<uint8_t> xb; read_file(xr.path, xb);
    std::string x(xb.begin(), xb.end());
    CHECK(x.find("<ProgramName>Test Kit</ProgramName>") != std::string::npos, "program name");
    CHECK(x.find("<Instrument number=\"128\">") != std::string::npos && x.find("<Instrument number=\"129\">") == std::string::npos, "128 instruments");
    CHECK(x.find("<SliceEnd>33688</SliceEnd>") == std::string::npos && x.find("<SliceEnd>4410</SliceEnd>") != std::string::npos, "SliceEnd is the real frame count");
    CHECK(x.find("\r\n") != std::string::npos, "CRLF line endings");
    CHECK(x.find("&quot;Type&quot;: {\r\n") == std::string::npos && x.find("&quot;Type&quot;: {\n            &quot;value0&quot;: 2") != std::string::npos, "program Type untouched by pad colours");
    CHECK(x.find("&quot;value0&quot;: 8323072") != std::string::npos, "pad 1 (kick) coloured 7f0000");
    std::vector<uint8_t> man; CHECK(read_file(root + "/out/Test Kit/MANIFEST.txt", man), "MANIFEST.txt written");
    CHECK(mpc_sample_name("/a/b/Deep Kick (01) & more!.wav") == "Deep Kick (01) _ more" , "sample name '%s'", mpc_sample_name("/a/b/Deep Kick (01) & more!.wav").c_str());

    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    std::string cmd = "rm -rf " + root; if (system(cmd.c_str())) {}
    return fails ? 1 : 0;
}
