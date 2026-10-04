/* Lucky Dip: the plugin engine (wrapper/engine.h). A 16-pad random drum-kit builder that plays its kit.
 *
 * Threads and what they may touch:
 *   audio (render, usually midi)  only the lock-free pad buffers, pending triggers and the voices.
 *   UI (set_param/get_param)      the kit (pads[]), under `mu`; never a disk read of audio, never a scan.
 *   worker (one per instance)     decodes samples, writes exports; publishes buffers with an atomic swap.
 *   scanner (on demand)           walks the sample folders, then swaps in the new shared library.
 * Retired sample buffers are freed only after RETIRE_SECS, longer than the longest pad (MAX_FRAMES), so a voice
 * still reading an old buffer never sees it freed. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <pthread.h>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
extern "C" {
#include "engine.h"       /* mpc-vst-plugins wrapper/engine.h, copied into vst/build/ by vst/build.sh */
}
#include "ld_audio.h"
#include "ld_core.h"
#include "ld_xpm.h"

using namespace ld;

namespace {

const int RETIRE_SECS = 20;
const char *const STATE_MAGIC = "LD1";

/* ---- the sample library, shared by every instance in the process ---- */
struct Shared {
    std::mutex mu;
    std::shared_ptr<Library> lib;            /* never null once initialised */
    std::string roots_sig;                   /* which roots the library was scanned from */
    bool scanning = false;
    size_t scan_files = 0;
    std::string prefs_path;
    Shared() : lib(new Library) { lib->rebuild(); }
};
Shared &shared() { static Shared s; return s; }

struct Config {
    ScanOpts scan;
    std::string export_dir;
};

std::string trim(const std::string &s) {
    size_t a = 0, e = s.size();
    while (a < e && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (e > a && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(a, e - a);
}
bool is_dir(const std::string &p) { struct stat s; return stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode); }

/* <data_dir>/luckydip.conf: root=<dir> (repeatable), export_dir=<dir>, skip_loops=0|1, max_mb=<n>,
 * classify_filenames=0|1. Without any root=, the usual card layout is searched (/media/<card>/Expansions etc.). */
Config load_config(const std::string &data_dir) {
    Config c;
    c.scan.skip_loops = true;
    FILE *f = fopen((data_dir + "/luckydip.conf").c_str(), "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            std::string s = trim(line);
            if (s.empty() || s[0] == '#') continue;
            size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(s.substr(0, eq)), v = trim(s.substr(eq + 1));
            if (k == "root" && !v.empty()) c.scan.roots.push_back(v);
            else if (k == "export_dir") c.export_dir = v;
            else if (k == "skip_loops") c.scan.skip_loops = atoi(v.c_str()) != 0;
            else if (k == "max_mb") c.scan.max_bytes = (int64_t)atof(v.c_str()) * 1024 * 1024;
            else if (k == "classify_filenames") c.scan.classify_filenames = atoi(v.c_str()) != 0;
        }
        fclose(f);
    }
    if (c.scan.roots.empty()) {
        DIR *d = opendir("/media");
        std::vector<std::string> cards;
        if (d) {
            while (struct dirent *e = readdir(d)) if (e->d_name[0] != '.') cards.push_back(std::string("/media/") + e->d_name);
            closedir(d);
        }
        std::sort(cards.begin(), cards.end());
        const char *const subs[] = {"Expansions", "Samples"};
        for (size_t i = 0; i < cards.size(); i++)
            for (size_t k = 0; k < 2; k++)
                if (is_dir(cards[i] + "/" + subs[k])) c.scan.roots.push_back(cards[i] + "/" + subs[k]);
        if (is_dir("/sdcard/Samples")) c.scan.roots.push_back("/sdcard/Samples");
    }
    if (c.export_dir.empty()) {
        std::string force = "/media/az01-internal-sd/Expansions/Kits & Patterns";   /* where a Force browses kits */
        c.export_dir = is_dir(force) ? force : data_dir + "/kits";
    }
    return c;
}

std::string roots_signature(const ScanOpts &o) {
    std::string s;
    for (size_t i = 0; i < o.roots.size(); i++) s += o.roots[i] + "|";
    s += o.skip_loops ? "L1" : "L0";
    s += "m" + std::to_string((long long)o.max_bytes) + (o.classify_filenames ? "c1" : "c0");
    return s;
}

/* index cache: line 1 "roots=<signature>", then "<cat>\t<source>\t<path>" per sample */
bool load_cache(const std::string &path, const std::string &sig, Library &lib) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return false;
    std::string line;
    char buf[4096];
    bool first = true, ok = false;
    while (fgets(buf, sizeof buf, f)) {
        line = trim(buf);
        if (first) { first = false; ok = line == "roots=" + sig; if (!ok) break; continue; }
        size_t a = line.find('\t');
        size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        Rec r;
        r.cat = atoi(line.substr(0, a).c_str());
        r.source = line.substr(a + 1, b - a - 1);
        r.path = line.substr(b + 1);
        if (r.cat >= 0 && r.cat < NCAT && !r.path.empty()) lib.recs.push_back(r);
    }
    fclose(f);
    return ok;
}
void save_cache(const std::string &path, const std::string &sig, const Library &lib) {
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (!f) return;
    fprintf(f, "roots=%s\n", sig.c_str());
    for (size_t i = 0; i < lib.recs.size(); i++)
        fprintf(f, "%d\t%s\t%s\n", lib.recs[i].cat, lib.recs[i].source.c_str(), lib.recs[i].path.c_str());
    fclose(f);
    rename(tmp.c_str(), path.c_str());
}
void load_prefs(const std::string &path, Library &lib) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return;
    char buf[4096];
    while (fgets(buf, sizeof buf, f)) {
        std::string s = trim(buf);
        if (s.size() < 3 || s[1] != '\t') continue;
        if (s[0] == 'F') lib.favourites.insert(s.substr(2));
        else if (s[0] == 'R') lib.rejects.insert(s.substr(2));
    }
    fclose(f);
}
void save_prefs(const std::string &path, const Library &lib) {
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (!f) return;
    for (std::set<std::string>::const_iterator i = lib.favourites.begin(); i != lib.favourites.end(); ++i) fprintf(f, "F\t%s\n", i->c_str());
    for (std::set<std::string>::const_iterator i = lib.rejects.begin(); i != lib.rejects.end(); ++i) fprintf(f, "R\t%s\n", i->c_str());
    fclose(f);
    rename(tmp.c_str(), path.c_str());
}

std::string short_name(const std::string &path, size_t max) {
    std::string b = base_name(path);
    size_t d = b.find_last_of('.');
    if (d != std::string::npos && d > 0) b = b.substr(0, d);
    if (b.size() > max) b = b.substr(0, max - 1) + "~";
    return b;
}

const int SIZE_MB[] = {0, 1, 2, 5, 10, 20};
const int NSIZES = 6;

/* A folder choice in two levels: a location (0 = "Default (auto)", then the folders directly under each card) and,
 * inside it, one subfolder (0 = the whole location), so a long list of packs is never one flat stepper. */
struct FolderSel {
    std::vector<std::string> locs;                 /* [0] = "" */
    std::vector<std::vector<std::string> > subs;   /* subs[i][0] = "" (whole location) */
    int loc = 0, sub = 0;
    FolderSel() { locs.assign(1, std::string()); subs.assign(1, std::vector<std::string>(1, std::string())); }
    std::string path() const {
        if (loc <= 0) return std::string();
        return sub > 0 ? subs[loc][sub] : locs[loc];
    }
    void set_path(const std::string &p) {
        loc = sub = 0;
        if (p.empty()) return;
        for (size_t i = 1; i < locs.size(); i++) {
            if (locs[i] == p) { loc = (int)i; return; }
            if (p.compare(0, locs[i].size() + 1, locs[i] + "/") == 0) {
                std::vector<std::string> &v = subs[i];
                size_t k = 0;
                while (k < v.size() && v[k] != p) k++;
                if (k == v.size()) v.push_back(p);
                loc = (int)i; sub = (int)k;
                return;
            }
        }
        locs.push_back(p); subs.push_back(std::vector<std::string>(1, std::string()));   /* a path from luckydip.conf, not found on the cards */
        loc = (int)locs.size() - 1;
    }
    void step_loc(int d) { int n = (int)locs.size(); loc = ((loc + d) % n + n) % n; sub = 0; }
    void step_sub(int d) {
        int n = (int)subs[loc].size();
        sub = n <= 1 ? 0 : std::max(0, std::min(n - 1, sub + d));        /* no wrap: a long list has ends */
    }
};

struct Job { enum Type { LOAD, EXPORT } type; int pad; std::string path; Pad pads[NPADS]; std::string dir, name; bool link = false; };

struct Retired { Pcm *p; std::chrono::steady_clock::time_point at; };

struct Inst {
    std::string data_dir;
    Config cfg;
    std::string sig, cache_path;
    std::vector<std::string> def_roots;                 /* the conf / card-layout defaults (source "Default") */
    std::string def_export;
    FolderSel src, exp;                                 /* the two folder choices on the SETTINGS page */
    bool link_samples = false;                          /* export: symlink the samples instead of copying them */
    int size_idx = 0;                                   /* index into SIZE_MB */

    std::mutex mu;                   /* pads[], status, export_name, sel, dup */
    Pad pads[NPADS];
    char loaded[NPADS];              /* 0 none, 1 playable, 2 failed to decode */
    float rms[NPADS];
    std::atomic<int> sel{0};            /* the selected pad (PAD EDIT page); follow moves it from the audio thread */
    pthread_t audio_tid; std::atomic<bool> audio_known{false};   /* the thread that calls render(): it must never block */
    std::atomic<uint32_t> lock_bits{0};              /* lock-free mirrors of pads[].locked / .pool for the "<key>_on" reads, which the */
    std::atomic<uint32_t> pool_bits[NPADS];          /* wrapper polls from the audio thread (and which must follow the selection) */
    std::atomic<bool> follow{true};     /* the selection follows the last pad played */
    bool prevent_dup = true;
    std::string status, export_name;
    uint32_t seed_ctr = 0;

    std::atomic<Pcm *> buf[NPADS];
    std::atomic<int> pending[NPADS]; /* note-on velocity waiting for the audio thread */
    struct Voice { const Pcm *p; uint32_t pos; float amp; } voice[NPADS];

    std::mutex jmu, gmu;             /* job queue; graveyard */
    std::condition_variable jcv;
    std::deque<Job> jobs;
    std::vector<Retired> grave;
    std::thread worker, scanner;
    volatile bool stop;

    Inst() : stop(false) {
        memset(loaded, 0, sizeof loaded);
        for (int i = 0; i < NPADS; i++) { pool_bits[i] = 0; rms[i] = 0; buf[i] = 0; pending[i] = 0; voice[i].p = 0; voice[i].pos = 0; voice[i].amp = 0; }
    }

    void set_status(const std::string &s) { std::lock_guard<std::mutex> l(mu); status = s.substr(0, 23); }
    uint32_t new_seed() {
        uint64_t t = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
        uint32_t s = (uint32_t)(t ^ (t >> 32)) * 2654435761u + (++seed_ctr) * 40503u + (uint32_t)time(0);
        return s ? s : 1;
    }
    void retire(Pcm *p) {
        if (!p) return;
        std::lock_guard<std::mutex> l(gmu);
        Retired r; r.p = p; r.at = std::chrono::steady_clock::now();
        grave.push_back(r);
    }
    void reap(bool all) {
        std::lock_guard<std::mutex> l(gmu);
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        for (size_t i = 0; i < grave.size();) {
            if (all || now - grave[i].at > std::chrono::seconds(RETIRE_SECS)) { delete grave[i].p; grave.erase(grave.begin() + i); }
            else i++;
        }
    }
    void push(const Job &j) { { std::lock_guard<std::mutex> l(jmu); jobs.push_back(j); } jcv.notify_one(); }

    /* queue a decode for every pad whose sample isn't playable yet (caller holds mu) */
    void queue_loads_locked() {
        for (int i = 0; i < NPADS; i++) {
            if (!pads[i].has || loaded[i] != 0) continue;
            Job j; j.type = Job::LOAD; j.pad = i; j.path = pads[i].sample.path;
            push(j);
        }
    }
    /* the categories a pad slot draws from when nothing was chosen */
    static uint32_t default_mask(int pad) {
        uint32_t m = 0;
        std::vector<int> c = pool_cats(pad, 0);
        for (size_t k = 0; k < c.size(); k++) m |= 1u << c[k];
        return m;
    }
    /* "<key>_on" mirrors: the lock bits, and each pad's EFFECTIVE pool (its choice, else its slot's default) so the
     * category keys show what the selected pad draws from */
    void sync_mirrors_locked() {
        uint32_t lk = 0;
        for (int i = 0; i < NPADS; i++) { if (pads[i].locked) lk |= 1u << i; pool_bits[i] = pads[i].pool ? pads[i].pool : default_mask(i); }
        lock_bits = lk;
    }
    void drop_pad_locked(int i) {
        loaded[i] = 0; rms[i] = 0;
        retire(buf[i].exchange(0));
    }

    void worker_main() {
        for (;;) {
            Job j; bool have = false;
            {
                std::unique_lock<std::mutex> l(jmu);
                if (jobs.empty() && !stop) jcv.wait_for(l, std::chrono::seconds(2));
                if (stop) return;
                if (!jobs.empty()) { j = jobs.front(); jobs.pop_front(); have = true; }
            }
            reap(false);
            if (!have) continue;
            if (j.type == Job::LOAD) do_load(j); else do_export(j);
        }
    }
    void do_load(const Job &j) {
        Pcm *p = new Pcm;
        bool ok = decode(j.path, *p);
        std::lock_guard<std::mutex> l(mu);
        if (!pads[j.pad].has || pads[j.pad].sample.path != j.path) { delete p; return; }   /* superseded */
        if (!ok) { delete p; loaded[j.pad] = 2; return; }
        rms[j.pad] = p->rms;
        retire(buf[j.pad].exchange(p));
        loaded[j.pad] = 1;
    }
    void do_export(const Job &j) {
        ExportResult r = export_xpm(j.dir, j.name, j.pads, j.link);
        std::lock_guard<std::mutex> l(mu);
        if (r.ok) { export_name = j.name; status = r.warnings.empty() ? "Exported OK" : "Exported (see log)"; }
        else status = "Export failed";
        if (!r.ok) fprintf(stderr, "luckydip: export failed: %s\n", r.error.c_str());
        for (size_t i = 0; i < r.warnings.size(); i++) fprintf(stderr, "luckydip: export: %s\n", r.warnings[i].c_str());
    }

    /* ---- settings: folder choices ---- */
    static void subdirs(const std::string &d, std::vector<std::string> &out) {
        DIR *dp = opendir(d.c_str());
        if (!dp) return;
        std::vector<std::string> n;
        while (struct dirent *e = readdir(dp)) if (e->d_name[0] != '.') n.push_back(e->d_name);
        closedir(dp);
        std::sort(n.begin(), n.end());
        for (size_t i = 0; i < n.size(); i++) if (is_dir(d + "/" + n[i])) out.push_back(d + "/" + n[i]);
    }
    /* what a person might pick: the folders directly under each card and the top level of /sdcard, each with its own
     * subfolders (so each pack inside Expansions is a second-level choice, not one more step in a flat list) */
    static void discover_dirs(std::vector<std::string> &locs, std::vector<std::vector<std::string> > &subs) {
        std::vector<std::string> top;
        std::vector<std::string> cards;
        subdirs("/media", cards);
        for (size_t c = 0; c < cards.size(); c++) {
            std::string nm = base_name(cards[c]);
            if (nm == "acvs-synths" || nm == "az01-internal") continue;     /* system mounts, not sample cards */
            subdirs(cards[c], top);
        }
        std::vector<std::string> sd;
        subdirs("/sdcard", sd);
        for (size_t i = 0; i < sd.size(); i++) {
            std::string nm = base_name(sd[i]);
            if (nm != "Synths" && nm != "MPC-backup") top.push_back(sd[i]);
        }
        locs.assign(1, std::string()); subs.assign(1, std::vector<std::string>(1, std::string()));
        for (size_t i = 0; i < top.size() && i < 60; i++) {
            locs.push_back(top[i]);
            std::vector<std::string> v(1, std::string());
            std::vector<std::string> kids;
            subdirs(top[i], kids);
            if (kids.size() > 500) kids.resize(500);
            v.insert(v.end(), kids.begin(), kids.end());
            subs.push_back(v);
        }
    }
    /* run on the scanner thread (it walks directories): installs the lists, keeping the folders already chosen */
    void install_folder_lists(const std::vector<std::string> &locs, const std::vector<std::vector<std::string> > &subs) {
        std::lock_guard<std::mutex> l(mu);
        std::string cs = src.path(), ce = exp.path();
        src.locs = exp.locs = locs; src.subs = exp.subs = subs;
        src.set_path(cs); exp.set_path(ce);
    }
    void apply_folders_locked() {
        std::string sp = src.path(), ep = exp.path();
        cfg.scan.roots = sp.empty() ? def_roots : std::vector<std::string>(1, sp);
        cfg.export_dir = ep.empty() ? def_export : ep;
        cfg.scan.max_bytes = (int64_t)SIZE_MB[size_idx] * 1024 * 1024;
    }
    static std::string folder_label(const std::string &p, const char *dflt, size_t max) {
        if (p.empty()) return dflt;
        std::string s = p;
        if (s.compare(0, 7, "/media/") == 0) s = s.substr(7);
        else if (s.compare(0, 1, "/") == 0) s = s.substr(1);
        if (s.size() > max) s = "~" + s.substr(s.size() - (max - 1));
        return s;
    }
    static std::string sub_label(const FolderSel &f) {
        if (f.loc <= 0) return "-";
        if (f.sub <= 0) return "(whole folder)";
        std::string pre = std::to_string(f.sub) + "/" + std::to_string(f.subs[f.loc].size() - 1) + " ";
        std::string nm = base_name(f.subs[f.loc][f.sub]);
        size_t room = 23 > pre.size() ? 23 - pre.size() : 1;
        if (nm.size() > room) nm = nm.substr(0, room - 1) + "~";
        return pre + nm;
    }
    /* which: 0 source location, 1 source inside, 2 export location, 3 export inside; d = steps */
    void step_folder(int which, int d) {
        std::lock_guard<std::mutex> l(mu);
        FolderSel &f = which < 2 ? src : exp;
        if (which % 2 == 0) f.step_loc(d); else f.step_sub(d);
        apply_folders_locked();
        status = which < 2 ? "Source set: 'RESCAN'" : "Export folder set";
    }

    /* ---- library ---- */
    /* A scan request while one of ours is running is queued, not dropped: a project load sets the saved source right
     * after create() started the first scan, and the library must end up matching it. */
    std::mutex smu;
    bool scan_active = false, again = false, again_force = false;
    void start_scan(bool force) {
        Shared &sh = shared();
        {
            std::lock_guard<std::mutex> l(smu);
            if (scan_active) { again = true; again_force = again_force || force; return; }
        }
        {
            std::lock_guard<std::mutex> l(sh.mu);
            if (sh.scanning) return;               /* another instance is scanning; its result is shared */
            sh.scanning = true; sh.scan_files = 0;
        }
        if (scanner.joinable()) scanner.join();
        { std::lock_guard<std::mutex> l(smu); scan_active = true; again = false; again_force = false; }
        set_status("Scanning...");
        ScanOpts opts;
        { std::lock_guard<std::mutex> l(mu); opts = cfg.scan; }
        std::string sg = roots_signature(opts);
        scanner = std::thread([this, force, opts, sg] { scan_main(force, opts, sg); });
    }
    void scan_pass(bool force, const ScanOpts &opts, const std::string &sig) {
        Shared &sh = shared();
        std::shared_ptr<Library> nl(new Library);
        bool cached = !force && load_cache(cache_path, sig, *nl);
        if (!cached) {
            nl->recs.clear();
            Classifier cl;
            ScanStats st;
            scan_library(opts, cl, nl->recs, st, &stop);
            if (!stop) save_cache(cache_path, sig, *nl);
        }
        nl->rebuild();
        std::lock_guard<std::mutex> l(sh.mu);
        if (!stop) {
            load_prefs(sh.prefs_path, *nl);
            sh.lib = nl; sh.roots_sig = sig;
        }
    }
    void scan_main(bool force, ScanOpts opts, std::string sig) {
        { std::vector<std::string> l; std::vector<std::vector<std::string> > sb; discover_dirs(l, sb); install_folder_lists(l, sb); }
        for (;;) {
            scan_pass(force, opts, sig);
            std::lock_guard<std::mutex> l(smu);
            if (stop || !again) break;
            again = false; force = again_force; again_force = false;
            { std::lock_guard<std::mutex> l2(mu); opts = cfg.scan; }
            sig = roots_signature(opts);
        }
        Shared &sh = shared();
        std::string msg;
        {
            std::lock_guard<std::mutex> l(sh.mu);
            sh.scanning = false;
            msg = sh.lib->recs.empty() ? "No samples found" : "Library: " + std::to_string(sh.lib->recs.size()) + " samples";
        }
        if (!stop) set_status(msg);
        std::lock_guard<std::mutex> l(smu);
        scan_active = false;
    }

    /* ---- kit actions (UI thread) ---- */
    void generate() {
        Shared &sh = shared();
        uint32_t seed = new_seed();
        AssignResult r;
        {
            std::lock_guard<std::mutex> l(sh.mu);
            std::lock_guard<std::mutex> l2(mu);
            if (sh.lib->recs.empty()) { status = sh.scanning ? "Scanning..." : "No samples found"; return; }
            std::string before[NPADS];
            for (int i = 0; i < NPADS; i++) before[i] = pads[i].has ? pads[i].sample.path : "";
            r = assign_kit(pads, *sh.lib, seed, prevent_dup);
            for (int i = 0; i < NPADS; i++)
                if (pads[i].has && pads[i].sample.path != before[i]) drop_pad_locked(i);
            queue_loads_locked();
            status = r.unresolved ? "Kit: " + std::to_string(NPADS - r.unresolved) + "/16 pads" : "Kit generated";
        }
    }
    void reroll(int i) {
        Shared &sh = shared();
        std::lock_guard<std::mutex> l(sh.mu);
        std::lock_guard<std::mutex> l2(mu);
        if (pads[i].locked) { status = "Pad " + std::to_string(i + 1) + " is locked"; return; }
        if (sh.lib->recs.empty()) { status = "No samples found"; return; }
        std::string before = pads[i].has ? pads[i].sample.path : "";
        if (reroll_pad(pads, i, *sh.lib, new_seed(), prevent_dup)) {
            if (pads[i].sample.path != before) drop_pad_locked(i);
            queue_loads_locked();
            status = "Pad " + std::to_string(i + 1) + " rerolled";
        } else status = "No sample for pad " + std::to_string(i + 1);
    }
    void clear_pad(int i) {
        std::lock_guard<std::mutex> l(mu);
        if (pads[i].locked) { status = "Pad " + std::to_string(i + 1) + " is locked"; return; }
        pads[i].has = false; pads[i].sample.path.clear();
        drop_pad_locked(i);
        status = "Pad " + std::to_string(i + 1) + " cleared";
    }
    void clear_all() {
        std::lock_guard<std::mutex> l(mu);
        for (int i = 0; i < NPADS; i++) if (!pads[i].locked) { pads[i].has = false; pads[i].sample.path.clear(); drop_pad_locked(i); }
        status = "Cleared";
    }
    void unlock_all() {
        std::lock_guard<std::mutex> l(mu);
        for (int i = 0; i < NPADS; i++) pads[i].locked = false;
        sync_mirrors_locked();
        status = "Unlocked all";
    }
    void normalise() {
        std::lock_guard<std::mutex> l(mu);
        float g[NPADS], r[NPADS];
        for (int i = 0; i < NPADS; i++) r[i] = pads[i].has && loaded[i] == 1 ? rms[i] : 0.0f;
        match_gains(r, g);
        for (int i = 0; i < NPADS; i++) pads[i].gain = g[i];
        status = "Levels matched";
    }
    void do_export_req() {
        std::lock_guard<std::mutex> l(mu);
        Job j; j.type = Job::EXPORT; j.pad = 0;
        int n = 0;
        for (int i = 0; i < NPADS; i++) { j.pads[i] = pads[i]; n += pads[i].has; }
        if (!n) { status = "Nothing to export"; return; }
        time_t t = time(0); struct tm tmv;
        localtime_r(&t, &tmv);
        char nm[48];
        strftime(nm, sizeof nm, "LuckyDip-%m%d-%H%M%S", &tmv);
        j.name = nm; j.dir = cfg.export_dir; j.link = link_samples;
        push(j);
        status = "Exporting...";
    }
    void favourite_reject(int i, bool reject) {
        Shared &sh = shared();
        std::string path;
        { std::lock_guard<std::mutex> l(mu); if (!pads[i].has) return; path = pads[i].sample.path; }
        std::lock_guard<std::mutex> l(sh.mu);
        /* mutually exclusive per sample, as in the web UI */
        sh.lib->favourites.erase(path); sh.lib->rejects.erase(path);
        (reject ? sh.lib->rejects : sh.lib->favourites).insert(path);
        save_prefs(sh.prefs_path, *sh.lib);
        std::lock_guard<std::mutex> l2(mu);
        status = reject ? "Rejected" : "Favourited";
    }
    void trigger(int pad, int vel) {
        if (pad < 0 || pad >= NPADS) return;
        pending[pad] = vel;
        if (follow.load()) sel = pad;       /* lock-free: this runs on the audio thread */
    }

    /* ---- state ---- */
    std::string get_state() {
        std::lock_guard<std::mutex> l(mu);
        std::string s = std::string(STATE_MAGIC) + "\nsel=" + std::to_string(sel.load()) + "\nfollow=" + (follow ? "1" : "0") + "\ndup=" + (prevent_dup ? "1" : "0") + "\n" +
                        "src=" + src.path() + "\nexp=" + exp.path() + "\nlink=" + (link_samples ? "1" : "0") +
                        "\nloops=" + (cfg.scan.skip_loops ? "1" : "0") + "\nsize=" + std::to_string(size_idx) + "\n";
        for (int i = 0; i < NPADS; i++) {
            const Pad &p = pads[i];
            char head[96];
            snprintf(head, sizeof head, "pad\t%d\t%d\t%u\t%d\t%.3f\t", i, p.locked ? 1 : 0, (unsigned)p.pool, p.has ? p.sample.cat : 0, p.gain);
            std::string line = std::string(head) + (p.has ? p.sample.path : "") + "\n";
            if (s.size() + line.size() > 7800) break;     /* the host's chunk buffer is 8 KiB: later pads are dropped, never half-written */
            s += line;
        }
        return s;
    }
    void set_state(const char *text) {
        std::string want;
        {
            std::lock_guard<std::mutex> l(mu);
            set_state_locked(text);
            if (folders_changed) { folders_changed = false; apply_folders_locked(); want = roots_signature(cfg.scan); }
        }
        if (want.empty()) return;
        bool differs;
        { Shared &sh = shared(); std::lock_guard<std::mutex> l(sh.mu); differs = sh.roots_sig != want; }   /* never both locks at once */
        if (differs) start_scan(false);              /* the saved source is not the library now loaded */
    }
    bool folders_changed = false;
    void set_state_locked(const char *text) {
        std::string s = text;
        size_t pos = 0;
        bool first = true;
        while (pos < s.size()) {
            size_t e = s.find('\n', pos);
            if (e == std::string::npos) e = s.size();
            std::string line = s.substr(pos, e - pos);
            pos = e + 1;
            if (first) { first = false; if (line != STATE_MAGIC) return; continue; }
            if (line.compare(0, 4, "sel=") == 0) sel = std::max(0, std::min(NPADS - 1, atoi(line.c_str() + 4)));
            else if (line.compare(0, 7, "follow=") == 0) follow = atoi(line.c_str() + 7) != 0;
            else if (line.compare(0, 4, "dup=") == 0) prevent_dup = atoi(line.c_str() + 4) != 0;
            else if (line.compare(0, 4, "src=") == 0) { src.set_path(line.substr(4)); folders_changed = true; }
            else if (line.compare(0, 4, "exp=") == 0) { exp.set_path(line.substr(4)); folders_changed = true; }
            else if (line.compare(0, 5, "link=") == 0) link_samples = atoi(line.c_str() + 5) != 0;
            else if (line.compare(0, 6, "loops=") == 0) { cfg.scan.skip_loops = atoi(line.c_str() + 6) != 0; folders_changed = true; }
            else if (line.compare(0, 5, "size=") == 0) { size_idx = std::max(0, std::min(NSIZES - 1, atoi(line.c_str() + 5))); folders_changed = true; }
            else if (line.compare(0, 4, "pad\t") == 0) {
                /* pad \t idx \t locked \t pool \t cat \t gain \t path */
                std::vector<std::string> f;
                size_t a = 0;
                while (f.size() < 6) {
                    size_t t = line.find('\t', a);
                    if (t == std::string::npos) break;
                    f.push_back(line.substr(a, t - a));
                    a = t + 1;
                }
                if (f.size() < 6) continue;
                std::string path = line.substr(a);
                int i = atoi(f[1].c_str());
                if (i < 0 || i >= NPADS) continue;
                Pad &p = pads[i];
                p.locked = atoi(f[2].c_str()) != 0;
                p.pool = (uint32_t)strtoul(f[3].c_str(), 0, 10) & ((1u << NCAT) - 1);
                p.gain = (float)atof(f[5].c_str());
                if (!(p.gain >= 0 && p.gain <= 2)) p.gain = 1;
                p.has = !path.empty();
                p.sample.path = path;
                p.sample.cat = std::max(0, std::min(NCAT - 1, atoi(f[4].c_str())));
                drop_pad_locked(i);
            }
        }
        sync_mirrors_locked();
        queue_loads_locked();
    }
};

/* "pad7_gain" -> pad 6, field "gain"; "sel_gain" -> the selected pad. false if not a pad key. */
bool split_key(Inst *in, const char *key, int *pad, const char **field) {
    if (!strncmp(key, "sel_", 4)) { *pad = in->sel; *field = key + 4; return true; }
    if (!strncmp(key, "pad", 3) && key[3] >= '0' && key[3] <= '9') {
        char *end;
        long n = strtol(key + 3, &end, 10);
        if (*end != '_' || n < 1 || n > NPADS) return false;
        *pad = (int)n - 1; *field = end + 1;
        return true;
    }
    return false;
}

void *e_create(const char *data_dir) {
    Inst *in = new Inst;
    in->data_dir = data_dir && *data_dir ? data_dir : "/tmp/luckydip";
    make_dirs(in->data_dir);
    in->cfg = load_config(in->data_dir);
    in->def_roots = in->cfg.scan.roots;
    in->def_export = in->cfg.export_dir;
    for (int i = 0; i < NSIZES; i++) if (SIZE_MB[i] * 1024LL * 1024 <= in->cfg.scan.max_bytes) in->size_idx = i;   /* conf's max_mb, rounded down to a choice */
    in->apply_folders_locked();
    in->sig = roots_signature(in->cfg.scan);
    in->cache_path = in->data_dir + "/index.cache";
    in->status = "Starting...";
    Shared &sh = shared();
    {
        std::lock_guard<std::mutex> l(sh.mu);
        if (sh.prefs_path.empty()) sh.prefs_path = in->data_dir + "/prefs.txt";
    }
    in->worker = std::thread([in] { in->worker_main(); });
    in->start_scan(false);       /* loads the cached index (or scans) and finds the folders the settings page offers */
    return in;
}

void e_destroy(void *h) {
    Inst *in = (Inst *)h;
    in->stop = true;
    in->jcv.notify_all();
    if (in->scanner.joinable()) in->scanner.join();
    if (in->worker.joinable()) in->worker.join();
    for (int i = 0; i < NPADS; i++) delete in->buf[i].exchange(0);
    in->reap(true);
    delete in;
}

void e_midi(void *h, const uint8_t *msg, int len) {
    Inst *in = (Inst *)h;
    if (len < 3 || (msg[0] & 0xF0) != 0x90 || msg[2] == 0) return;
    int n = msg[1], pad = -1;
    if (n < NPADS) pad = n;                      /* the drum-pad layout sends pad n as note n-1 */
    else if (n >= 36 && n < 36 + NPADS) pad = n - 36;
    if (pad >= 0) in->trigger(pad, msg[2]);
}

bool is_on(const char *v) { return atof(v) > 0.5; }

void e_set_param(void *h, const char *key, const char *val) {
    Inst *in = (Inst *)h;
    if (!strcmp(key, "state")) { in->set_state(val); return; }
    if (!strcmp(key, "generate")) { if (is_on(val)) in->generate(); return; }
    if (!strcmp(key, "clear_all")) { if (is_on(val)) in->clear_all(); return; }
    if (!strcmp(key, "unlock_all")) { if (is_on(val)) in->unlock_all(); return; }
    if (!strcmp(key, "normalise")) { if (is_on(val)) in->normalise(); return; }
    if (!strcmp(key, "export")) { if (is_on(val)) in->do_export_req(); return; }
    if (!strcmp(key, "rescan")) { if (is_on(val)) in->start_scan(true); return; }
    {   /* SETTINGS: <src|exp>_<loc|sub>_<prev|next|prev10|next10> */
        static const char *const F[] = {"src_loc", "src_sub", "exp_loc", "exp_sub"};
        for (int w = 0; w < 4; w++) {
            size_t fl = strlen(F[w]);
            if (strncmp(key, F[w], fl) || key[fl] != '_') continue;
            const char *v = key + fl + 1;
            if (!is_on(val)) return;
            if (!strcmp(v, "prev")) in->step_folder(w, -1);
            else if (!strcmp(v, "next")) in->step_folder(w, 1);
            else if (!strcmp(v, "prev10")) in->step_folder(w, -10);
            else if (!strcmp(v, "next10")) in->step_folder(w, 10);
            return;
        }
    }
    if (!strcmp(key, "export_mode")) { std::lock_guard<std::mutex> l(in->mu); in->link_samples = atoi(val) == 1; return; }
    if (!strcmp(key, "skip_loops")) { std::lock_guard<std::mutex> l(in->mu); in->cfg.scan.skip_loops = is_on(val); in->status = "'RESCAN' to apply"; return; }
    if (!strcmp(key, "max_size")) { std::lock_guard<std::mutex> l(in->mu); in->size_idx = std::max(0, std::min(NSIZES - 1, atoi(val))); in->apply_folders_locked(); in->status = "'RESCAN' to apply"; return; }
    if (!strcmp(key, "prevent_dup")) { std::lock_guard<std::mutex> l(in->mu); in->prevent_dup = is_on(val); return; }
    if (!strcmp(key, "follow")) { in->follow = is_on(val); return; }
    if (!strcmp(key, "sel_pad")) {
        std::lock_guard<std::mutex> l(in->mu);
        in->sel = std::max(0, std::min(NPADS - 1, (int)(atof(val) + 0.5) - 1));
        return;
    }
    int pad; const char *f;
    if (!split_key(in, key, &pad, &f)) return;
    if (!strcmp(f, "gain")) { std::lock_guard<std::mutex> l(in->mu); in->pads[pad].gain = (float)std::max(0.0, std::min(200.0, atof(val))) / 100.0f; }
    else if (!strcmp(f, "lock")) { std::lock_guard<std::mutex> l(in->mu); in->pads[pad].locked = is_on(val); in->sync_mirrors_locked(); }
    else if (!strncmp(f, "cat_", 4)) {      /* sel_cat_<name>: one category in/out of the pad's pool */
        for (int k = 0; k < NCAT; k++) if (!strcmp(f + 4, CAT_NAME[k])) {
            std::lock_guard<std::mutex> l(in->mu);
            {   /* toggle one category in the pad's effective pool; ending up on the slot's default (or empty) means "default" again */
                uint32_t def = Inst::default_mask(pad), cur = in->pads[pad].pool ? in->pads[pad].pool : def;
                if (is_on(val)) cur |= 1u << k; else cur &= ~(1u << k);
                in->pads[pad].pool = (cur == def) ? 0 : cur;
            }
            in->sync_mirrors_locked();
        }
    }
    else if (!strcmp(f, "reroll")) { if (is_on(val)) in->reroll(pad); }
    else if (!strcmp(f, "clear")) { if (is_on(val)) in->clear_pad(pad); }
    else if (!strcmp(f, "play")) { if (is_on(val)) in->trigger(pad, 100); }
    else if (!strcmp(f, "fav")) { if (is_on(val)) in->favourite_reject(pad, false); }
    else if (!strcmp(f, "reject")) { if (is_on(val)) in->favourite_reject(pad, true); }
}

int put(char *buf, int n, const std::string &s) { snprintf(buf, n, "%s", s.c_str()); return (int)strlen(buf) + 1; }

int e_get_param(void *h, const char *key, char *buf, int n) {
    Inst *in = (Inst *)h;
    if (n <= 0) return 0;
    size_t kl = strlen(key);
    if (kl > 3 && !strcmp(key + kl - 3, "_on")) {     /* the wrapper polls these from the audio thread: lock-free, from the mirrors */
        std::string base(key, kl - 3);
        int pad; const char *f;
        if (!split_key(in, base.c_str(), &pad, &f)) return 0;
        if (!strcmp(f, "lock")) return put(buf, n, (in->lock_bits.load() >> pad) & 1 ? "1" : "0");
        if (!strncmp(f, "cat_", 4))
            for (int k = 0; k < NCAT; k++) if (!strcmp(f + 4, CAT_NAME[k])) return put(buf, n, (in->pool_bits[pad].load() >> k) & 1 ? "1" : "0");
        return 0;
    }
    if (!strcmp(key, "state")) return put(buf, n, in->get_state());
    /* The wrapper polls every text param from the audio thread (to notice text that changed by itself): there a lock that is
     * busy (Generate holds them for a few ms) means "no text this time", never a wait. Other threads wait as usual. */
    bool audio = in->audio_known.load() && pthread_equal(pthread_self(), in->audio_tid);
    if (!strcmp(key, "lib_info")) {
        Shared &sh = shared();
        std::unique_lock<std::mutex> l(sh.mu, std::defer_lock);
        if (audio) { if (!l.try_lock()) return 0; } else l.lock();
        if (sh.scanning) return put(buf, n, "Scanning...");
        return put(buf, n, sh.lib->recs.empty() ? "No samples" : std::to_string(sh.lib->recs.size()) + " samples");
    }
    std::unique_lock<std::mutex> l(in->mu, std::defer_lock);
    if (audio) { if (!l.try_lock()) return 0; } else l.lock();
    if (!strcmp(key, "src_loc_name")) return put(buf, n, Inst::folder_label(in->src.locs[in->src.loc], "Default (auto)", 23));
    if (!strcmp(key, "exp_loc_name")) return put(buf, n, Inst::folder_label(in->exp.locs[in->exp.loc], "Default (auto)", 23));
    if (!strcmp(key, "src_sub_name")) return put(buf, n, Inst::sub_label(in->src));
    if (!strcmp(key, "exp_sub_name")) return put(buf, n, Inst::sub_label(in->exp));
    if (!strncmp(key, "src_loc_", 8) || !strncmp(key, "src_sub_", 8) || !strncmp(key, "exp_loc_", 8) || !strncmp(key, "exp_sub_", 8)) return put(buf, n, "0");   /* triggers */
    if (!strcmp(key, "export_mode")) return put(buf, n, in->link_samples ? "1" : "0");
    if (!strcmp(key, "skip_loops")) return put(buf, n, in->cfg.scan.skip_loops ? "1" : "0");
    if (!strcmp(key, "max_size")) return put(buf, n, std::to_string(in->size_idx));
    if (!strcmp(key, "status")) return put(buf, n, in->status);
    if (!strcmp(key, "export_name")) return put(buf, n, in->export_name.empty() ? "-" : in->export_name);
    if (!strcmp(key, "prevent_dup")) return put(buf, n, in->prevent_dup ? "1" : "0");
    if (!strcmp(key, "sel_pad")) return put(buf, n, std::to_string(in->sel.load() + 1));
    if (!strcmp(key, "follow")) return put(buf, n, in->follow ? "1" : "0");
    int pad; const char *f;
    if (!split_key(in, key, &pad, &f)) return 0;
    const Pad &p = in->pads[pad];
    if (!strcmp(f, "name") || !strcmp(f, "name_full")) {
        if (!p.has) return put(buf, n, "(empty)");
        std::string s = short_name(p.sample.path, 23);
        if (in->loaded[pad] == 2) s = "! " + short_name(p.sample.path, 21);
        return put(buf, n, s);
    }
    if (!strncmp(f, "cat_", 4)) {            /* a text param (its look is baked into the skin): the label; the state is "<key>_on" */
        for (int k = 0; k < NCAT; k++) if (!strcmp(f + 4, CAT_NAME[k])) return put(buf, n, CAT_SHORT[k]);
        return 0;
    }
    if (!strcmp(f, "cat")) return put(buf, n, p.has ? CAT_NAME[p.sample.cat] : "-");
    if (!strcmp(f, "pill")) {
        std::string s = p.has ? CAT_SHORT[p.sample.cat] : "-";
        if (p.pool) {       /* an override: the first chosen category, "+" when several */
            int first = 0, cnt = 0;
            for (int k = NCAT - 1; k >= 0; k--) if ((p.pool >> k) & 1) { first = k; cnt++; }
            s = std::string(CAT_SHORT[first]) + (cnt > 1 ? "+" : "");
        }
        return put(buf, n, s);
    }
    if (!strcmp(f, "gain")) return put(buf, n, std::to_string((int)(p.gain * 100.0f + 0.5f)));
    if (!strcmp(f, "lock")) return put(buf, n, "LOCK");
        if (!strcmp(f, "reroll") || !strcmp(f, "play") || !strcmp(f, "clear") || !strcmp(f, "fav") || !strcmp(f, "reject")) return put(buf, n, "0");
    return 0;
}

void e_render(void *h, int16_t *out, int frames) {
    Inst *in = (Inst *)h;
    if (!in->audio_known.load()) { in->audio_tid = pthread_self(); in->audio_known = true; }
    for (int p = 0; p < NPADS; p++) {
        int v = in->pending[p].exchange(0);
        if (!v) continue;
        const Pcm *pcm = in->buf[p].load();
        if (!pcm) continue;
        float g;
        { /* the gain is a plain float written by the UI thread: a torn read is harmless, a lock here is not allowed */
            g = in->pads[p].gain;
        }
        in->voice[p].p = pcm; in->voice[p].pos = 0; in->voice[p].amp = (v / 127.0f) * g;
    }
    float mix[256 * 2];
    int n = frames > 256 ? 256 : frames;
    memset(mix, 0, sizeof(float) * n * 2);
    for (int p = 0; p < NPADS; p++) {
        Inst::Voice &vo = in->voice[p];
        if (!vo.p) continue;
        uint32_t total = vo.p->frames();
        const int16_t *s = &vo.p->lr[0];
        int k = 0;
        for (; k < n && vo.pos < total; k++, vo.pos++) {
            mix[k * 2] += s[vo.pos * 2] * vo.amp;
            mix[k * 2 + 1] += s[vo.pos * 2 + 1] * vo.amp;
        }
        if (vo.pos >= total) vo.p = 0;
    }
    for (int i = 0; i < n * 2; i++) {
        float x = mix[i];
        out[i] = (int16_t)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : x);
    }
    for (int i = n * 2; i < frames * 2; i++) out[i] = 0;
}

const mpc_engine_t ENGINE = {e_create, e_destroy, e_midi, e_set_param, e_get_param, e_render, 0};

}  // namespace

extern "C" const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
