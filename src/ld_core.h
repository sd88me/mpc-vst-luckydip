/* Lucky Dip core: sample classification, library scan, seeded kit assignment, loudness matching.
 * A C++11 port of force-kit-builder's core/ (sample_classifier, sample_index, scan_filters, random_assign,
 * loudness, kit_model, pad_colors). Pure logic plus plain POSIX directory walking: no host or plugin code,
 * so tests/core_test.cpp can exercise it on x86. Behaviour matches the JS original; where a rule is
 * subtle the comment says which JS function it mirrors. */
#pragma once
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ld {

enum { NCAT = 23, NPADS = 16 };

/* ROLE_ORDER in sample_index.mjs; the index in this table is the category id used everywhere. */
enum Cat {
    C_KICK, C_SNARE, C_RIM, C_CLAP, C_HAT, C_CLOSED_HAT, C_OPEN_HAT, C_TOM, C_CONGA, C_PERC, C_CRASH, C_RIDE,
    C_CYMBAL, C_FX, C_GLITCH, C_VOX, C_BASS, C_SYNTH, C_STAB, C_CHORD, C_LEAD, C_PAD, C_OTHER
};
static const char *const CAT_NAME[NCAT] = {
    "kick", "snare", "rim", "clap", "hat", "closed_hat", "open_hat", "tom", "conga", "percussion", "crash", "ride",
    "cymbal", "fx", "glitch", "vox", "bass", "synth", "stab", "chord", "lead", "pad", "other"};
/* short pill text for the touchscreen (the shadow page used abbreviations too) */
static const char *const CAT_SHORT[NCAT] = {
    "KICK", "SNARE", "RIM", "CLAP", "HAT", "C.HAT", "O.HAT", "TOM", "CONGA", "PERC", "CRASH", "RIDE",
    "CYM", "FX", "GLITCH", "VOX", "BASS", "SYNTH", "STAB", "CHORD", "LEAD", "PAD", "OTHER"};
/* pad_colors.mjs DEFAULT_PAD_COLORS (the Akai factory-kit convention), 0xRRGGBB */
static const uint32_t CAT_COLOR[NCAT] = {
    0x7f0000, 0x7f7f00, 0x7f7f00, 0x7f7f00, 0x5f3300, 0x5f3300, 0x5f3300, 0x229ed0, 0x229ed0, 0x229ed0, 0x5f3300,
    0x5f3300, 0x5f3300, 0xee2288, 0xee2288, 0x00007f, 0x007f00, 0xa000ff, 0x50007f, 0xa000ff, 0x50007f, 0xa000ff,
    0x555555};

/* ---- classifier (sample_classifier.mjs) ----------------------------------------------------------------------- */

struct Alias { const char *name; int cat; };
/* DEFAULT_CONFIG.role_rules folder_aliases, in role order: the first writer of a normalised alias wins
 * ("shaker" is listed under hat and percussion and resolves to hat). */
static const Alias ALIASES[] = {
    {"kick", C_KICK}, {"kicks", C_KICK}, {"kck", C_KICK}, {"bd", C_KICK}, {"bass drum", C_KICK},
    {"snare", C_SNARE}, {"snares", C_SNARE}, {"snr", C_SNARE}, {"sd", C_SNARE},
    {"rim", C_RIM}, {"rims", C_RIM}, {"rimshot", C_RIM}, {"rimshots", C_RIM}, {"side stick", C_RIM},
    {"clap", C_CLAP}, {"claps", C_CLAP}, {"clp", C_CLAP}, {"cp", C_CLAP}, {"hand clap", C_CLAP},
    {"hat", C_HAT}, {"hats", C_HAT}, {"hihat", C_HAT}, {"hihats", C_HAT}, {"hi hat", C_HAT}, {"hi hats", C_HAT},
    {"hh", C_HAT}, {"shaker", C_HAT}, {"shakers", C_HAT},
    {"closed hat", C_CLOSED_HAT}, {"closed hats", C_CLOSED_HAT}, {"closed hihat", C_CLOSED_HAT},
    {"closed hihats", C_CLOSED_HAT}, {"closed hh", C_CLOSED_HAT}, {"hihat closed", C_CLOSED_HAT},
    {"hh closed", C_CLOSED_HAT}, {"ch", C_CLOSED_HAT}, {"chh", C_CLOSED_HAT}, {"hh c", C_CLOSED_HAT},
    {"hat c", C_CLOSED_HAT},
    {"open hat", C_OPEN_HAT}, {"open hats", C_OPEN_HAT}, {"open hihat", C_OPEN_HAT}, {"open hihats", C_OPEN_HAT},
    {"open hh", C_OPEN_HAT}, {"hihat open", C_OPEN_HAT}, {"hh open", C_OPEN_HAT}, {"oh", C_OPEN_HAT},
    {"ohh", C_OPEN_HAT}, {"hh o", C_OPEN_HAT}, {"hat o", C_OPEN_HAT},
    {"tom", C_TOM}, {"toms", C_TOM}, {"floor", C_TOM}, {"rack", C_TOM}, {"rototom", C_TOM}, {"rototoms", C_TOM},
    {"timbale", C_TOM}, {"timbales", C_TOM},
    {"conga", C_CONGA}, {"congas", C_CONGA},
    {"percussion", C_PERC}, {"perc", C_PERC}, {"percs", C_PERC}, {"tambourine", C_PERC}, {"tambourines", C_PERC},
    {"tamb", C_PERC}, {"cowbell", C_PERC}, {"cowbells", C_PERC}, {"bongo", C_PERC}, {"bongos", C_PERC},
    {"agogo", C_PERC}, {"woodblock", C_PERC}, {"woodblocks", C_PERC}, {"wood", C_PERC}, {"block", C_PERC},
    {"triangle", C_PERC}, {"triangles", C_PERC}, {"cabasa", C_PERC}, {"maracas", C_PERC}, {"guiro", C_PERC},
    {"guiros", C_PERC}, {"claves", C_PERC}, {"shaker", C_PERC}, {"shakers", C_PERC},
    {"crash", C_CRASH}, {"crashes", C_CRASH},
    {"ride", C_RIDE}, {"rides", C_RIDE},
    {"cymbal", C_CYMBAL}, {"cymbals", C_CYMBAL}, {"cym", C_CYMBAL},
    {"fx", C_FX}, {"sfx", C_FX}, {"effect", C_FX}, {"effects", C_FX}, {"sound fx", C_FX}, {"sound effects", C_FX},
    {"noise", C_FX}, {"noises", C_FX}, {"foley", C_FX}, {"impact", C_FX}, {"impacts", C_FX}, {"hit", C_FX},
    {"hits", C_FX}, {"riser", C_FX}, {"risers", C_FX}, {"sweep", C_FX}, {"sweeps", C_FX}, {"transition", C_FX},
    {"transitions", C_FX},
    {"glitch", C_GLITCH}, {"glitches", C_GLITCH}, {"glitchy", C_GLITCH}, {"grain", C_GLITCH},
    {"grains", C_GLITCH}, {"granular", C_GLITCH},
    {"vox", C_VOX}, {"vocal", C_VOX}, {"vocals", C_VOX}, {"voice", C_VOX}, {"voices", C_VOX}, {"chant", C_VOX},
    {"chants", C_VOX}, {"choir", C_VOX},
    {"bass", C_BASS}, {"basses", C_BASS}, {"sub", C_BASS}, {"subs", C_BASS},
    {"synth", C_SYNTH}, {"synths", C_SYNTH}, {"synthesizer", C_SYNTH}, {"analog", C_SYNTH},
    {"stab", C_STAB}, {"stabs", C_STAB}, {"chord hit", C_STAB},
    {"chord", C_CHORD}, {"chords", C_CHORD},
    {"lead", C_LEAD}, {"leads", C_LEAD}, {"melody", C_LEAD}, {"melodic", C_LEAD}, {"melodies", C_LEAD},
    {"pad", C_PAD}, {"pads", C_PAD}, {"atmosphere", C_PAD}, {"ambient", C_PAD}, {"texture", C_PAD},
    {"textures", C_PAD}, {"drone", C_PAD}, {"drones", C_PAD}, {"strings", C_PAD}, {"keys", C_PAD},
    {"piano", C_PAD}, {"organ", C_PAD}, {"brass", C_PAD},
    /* "other" has no aliases of its own that matter: buildAliasIndex skips the role entirely. */
};

/* lowercase, trim, drop every space / underscore / hyphen (normalizeToken) */
inline std::string normalize_token(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '_' || c == '-' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') continue;
        o += (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    return o;
}

class Classifier {
public:
    Classifier() {
        for (size_t i = 0; i < sizeof ALIASES / sizeof ALIASES[0]; i++) {
            std::string k = normalize_token(ALIASES[i].name);
            if (!k.empty() && !idx_.count(k)) idx_[k] = ALIASES[i].cat;   /* first writer wins */
        }
    }
    int lookup(const std::string &norm) const {
        std::map<std::string, int>::const_iterator it = idx_.find(norm);
        return it == idx_.end() ? -1 : it->second;
    }
    /* tokenizeFilename: basename, no extension; split on lower->Upper, letter<->digit and anything not [a-z0-9] */
    static std::vector<std::string> tokenize(const std::string &name) {
        std::string b = name;
        size_t sl = b.find_last_of("/\\");
        if (sl != std::string::npos) b = b.substr(sl + 1);
        size_t dot = b.find_last_of('.');
        if (dot != std::string::npos && dot > 0) b = b.substr(0, dot);
        else if (dot == 0) b = "";
        std::vector<std::string> out;
        std::string cur;
        char prev = 0;
        for (size_t i = 0; i < b.size(); i++) {
            char c = b[i];
            bool up = c >= 'A' && c <= 'Z', lo = c >= 'a' && c <= 'z', dg = c >= '0' && c <= '9';
            if (!(up || lo || dg)) { if (!cur.empty()) out.push_back(cur); cur.clear(); prev = 0; continue; }
            bool pl = prev >= 'a' && prev <= 'z', pu = prev >= 'A' && prev <= 'Z', pd = prev >= '0' && prev <= '9';
            if (!cur.empty() && ((pl && up) || ((pl || pu) && dg) || (pd && (lo || up)))) {
                out.push_back(cur); cur.clear();
            }
            cur += (char)(up ? c + 32 : c);
            prev = c;
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }
    /* classifyFilename: contiguous joins of up to 3 tokens, longest window first, first hit wins */
    int classify_filename(const std::string &filename) const {
        std::vector<std::string> t = tokenize(filename);
        int n = (int)t.size();
        for (int win = std::min(3, n); win >= 1; win--)
            for (int i = 0; i + win <= n; i++) {
                std::string j;
                for (int k = 0; k < win; k++) j += t[i + k];
                int h = lookup(j);
                if (h >= 0) return h;
            }
        return C_OTHER;
    }
    /* classify(dirComponents, aliasIndex, filename): deepest folder match wins; a filename fallback when the
     * folders give nothing; a generic "hat" folder is promoted by a specific closed/open filename. */
    int classify(const std::vector<std::string> &dirs, const std::string &filename, bool use_filename = true) const {
        int role = C_OTHER;
        for (size_t i = 0; i < dirs.size(); i++) {
            int h = lookup(normalize_token(dirs[i]));
            if (h >= 0) role = h;
        }
        if (!use_filename || filename.empty()) return role;
        if (role == C_OTHER) role = classify_filename(filename);
        else if (role == C_HAT) {
            int fn = classify_filename(filename);
            if (fn == C_CLOSED_HAT || fn == C_OPEN_HAT) role = fn;
        }
        return role;
    }
private:
    std::map<std::string, int> idx_;
};

/* ---- scan filters (scan_filters.mjs) -------------------------------------------------------------------------- */

inline std::string lower(const std::string &s) {
    std::string o = s;
    for (size_t i = 0; i < o.size(); i++) if (o[i] >= 'A' && o[i] <= 'Z') o[i] += 32;
    return o;
}
/* "loop" anywhere, a bracketed number ("[120", "[ 130bpm]"), or "<n> bpm" */
inline bool looks_like_loop(const std::string &filename) {
    std::string n = lower(filename);
    if (n.find("loop") != std::string::npos) return true;
    for (size_t i = 0; i < n.size(); i++) {
        if (n[i] == '[') {
            size_t j = i + 1;
            while (j < n.size() && (n[j] == ' ' || n[j] == '\t')) j++;
            if (j < n.size() && n[j] >= '0' && n[j] <= '9') return true;
        }
        if (n[i] >= '0' && n[i] <= '9') {
            size_t j = i;
            while (j < n.size() && n[j] >= '0' && n[j] <= '9') j++;
            while (j < n.size() && (n[j] == ' ' || n[j] == '\t')) j++;
            if (n.compare(j, 3, "bpm") == 0) return true;
            i = j > i ? j - 1 : i;
        }
    }
    return false;
}

/* ---- library index (sample_index.mjs) ------------------------------------------------------------------------- */

struct Rec {
    std::string path;     /* absolute file path */
    std::string source;   /* the root it was found under */
    int cat;
};

struct ScanOpts {
    std::vector<std::string> roots;
    bool skip_loops = true;
    int64_t max_bytes = 0;      /* 0 = no cap */
    bool classify_filenames = true;
    int max_depth = 8;
    size_t max_records = 200000;
};
struct ScanStats { size_t files = 0, dirs = 0, loops = 0, oversize = 0; };

inline bool has_audio_ext(const std::string &lname) {
    size_t d = lname.find_last_of('.');
    if (d == std::string::npos) return false;
    std::string e = lname.substr(d);
    return e == ".wav" || e == ".aif" || e == ".aiff";
}

/* Depth-first walk of every root. Hidden entries and symlinks are skipped (no loops, no escaping the root),
 * the depth is capped, and `stop` (optional) is polled so a rescan can be abandoned. */
inline void scan_library(const ScanOpts &o, const Classifier &cl, std::vector<Rec> &out, ScanStats &st,
                         const volatile bool *stop = 0) {
    struct Item { std::string dir; std::vector<std::string> rel; };
    for (size_t r = 0; r < o.roots.size(); r++) {
        std::string root = o.roots[r];
        while (root.size() > 1 && root[root.size() - 1] == '/') root.erase(root.size() - 1);
        struct stat rs;
        if (root.empty() || stat(root.c_str(), &rs) != 0 || !S_ISDIR(rs.st_mode)) continue;
        std::vector<Item> stack;
        /* the root folder's own name counts as a folder component, so choosing "Kicks" itself as the source still classifies
         * its samples as kicks (the JS original only ever saw names below the roots) */
        size_t rsl = root.find_last_of('/');
        std::string rname = rsl == std::string::npos ? root : root.substr(rsl + 1);
        stack.push_back(Item{root, std::vector<std::string>(1, rname)});
        while (!stack.empty()) {
            if (stop && *stop) return;
            Item it = stack.back(); stack.pop_back();
            DIR *d = opendir(it.dir.c_str());
            if (!d) continue;
            st.dirs++;
            std::vector<std::string> names;
            while (struct dirent *e = readdir(d)) {
                if (e->d_name[0] == '.') continue;
                names.push_back(e->d_name);
            }
            closedir(d);
            std::sort(names.begin(), names.end());   /* stable order: a scan is repeatable */
            for (size_t i = 0; i < names.size(); i++) {
                std::string full = it.dir + "/" + names[i];
                struct stat s;
                if (lstat(full.c_str(), &s) != 0 || S_ISLNK(s.st_mode)) continue;
                if (S_ISDIR(s.st_mode)) {
                    if (names[i].compare(0, 9, "LuckyDip-") == 0) continue;   /* our own exported kits are not source material */
                    if ((int)it.rel.size() <= o.max_depth) {
                        Item n; n.dir = full; n.rel = it.rel; n.rel.push_back(names[i]);
                        stack.push_back(n);
                    }
                    continue;
                }
                if (!S_ISREG(s.st_mode)) continue;
                st.files++;
                std::string ln = lower(names[i]);
                if (!has_audio_ext(ln)) continue;
                if (o.skip_loops && looks_like_loop(names[i])) { st.loops++; continue; }
                if (o.max_bytes > 0 && (int64_t)s.st_size > o.max_bytes) { st.oversize++; continue; }
                if (out.size() >= o.max_records) return;
                Rec rec;
                rec.path = full; rec.source = root;
                rec.cat = cl.classify(it.rel, names[i], o.classify_filenames);
                out.push_back(rec);
            }
        }
    }
}

/* ---- kit model + assignment (kit_model.mjs, random_assign.mjs) ------------------------------------------------ */

/* DEFAULT_PAD_LAYOUT: the category union each pad draws from. An empty list is the "other" sentinel:
 * every category with no dedicated slot, plus fx. */
static const int8_t PAD_LAYOUT[NPADS][4] = {
    {C_KICK, -1},
    {C_SNARE, -1},
    {C_RIM, C_SNARE, -1},
    {C_CLAP, C_PERC, -1},
    {C_HAT, C_CLOSED_HAT, C_OPEN_HAT, -1},
    {C_CLOSED_HAT, C_HAT, -1},
    {C_OPEN_HAT, C_HAT, -1},
    {C_PERC, -1},
    {C_PERC, C_TOM, C_CONGA, -1},
    {C_TOM, C_PERC, C_CONGA, -1},
    {C_RIDE, C_CYMBAL, C_CRASH, -1},
    {-1}, {-1}, {-1}, {-1},        /* 12-15: the catch-all pools */
    {C_FX, -1}};

/* mulberry32: same seed, same sequence (with the fixed pad order, a kit is repeatable) */
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    double next() {
        s += 0x6D2B79F5u;
        uint32_t t = s;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return (double)(t ^ (t >> 14)) / 4294967296.0;
    }
};

struct PadSample { std::string path; int cat; };
struct Pad {
    bool has = false;
    PadSample sample;
    bool locked = false;
    uint32_t pool = 0;      /* per-pad pool override: a set of categories (bit n = category n); 0 = the layout's default */
    float gain = 1.0f;      /* 0..2, 1 = unity */
};

/* categories with no dedicated slot (otherPoolCats), plus fx */
inline std::vector<int> other_pool() {
    bool slotted[NCAT] = {false};
    for (int p = 0; p < NPADS; p++) {
        if (PAD_LAYOUT[p][0] < 0) continue;
        for (int k = 0; PAD_LAYOUT[p][k] >= 0; k++) slotted[PAD_LAYOUT[p][k]] = true;
    }
    std::vector<int> o;
    for (int c = 0; c < NCAT; c++) if (!slotted[c]) o.push_back(c);
    if (std::find(o.begin(), o.end(), (int)C_FX) == o.end()) o.push_back(C_FX);
    return o;
}
inline std::vector<int> pool_cats(int pad, uint32_t override_mask) {
    std::vector<int> c;
    if (override_mask & ((1u << NCAT) - 1)) {
        for (int k = 0; k < NCAT; k++) if (override_mask & (1u << k)) c.push_back(k);
        return c;
    }
    if (PAD_LAYOUT[pad][0] < 0) return other_pool();
    for (int k = 0; PAD_LAYOUT[pad][k] >= 0; k++) c.push_back(PAD_LAYOUT[pad][k]);
    return c;
}

struct Library {
    std::vector<Rec> recs;
    std::set<std::string> rejects, favourites;     /* library-wide preferences (paths) */
    std::vector<std::vector<int> > by_cat;          /* record indices per category */
    void rebuild() {
        by_cat.assign(NCAT, std::vector<int>());
        for (size_t i = 0; i < recs.size(); i++)
            if (recs[i].cat >= 0 && recs[i].cat < NCAT) by_cat[recs[i].cat].push_back((int)i);
    }
};

/* resolveOne: pick one record for a pad from its category union.
 * `used` = paths already taken. Avoids the pad's own current sample; relaxes duplicates when the unique pool
 * runs dry (never relaxes rejects); favourites get a second entry (~2x as likely). Returns -1 when empty. */
inline int resolve_one(const Pad &pad, const std::vector<int> &cats, const Library &lib, Rng &rng,
                       const std::set<std::string> &used, bool prevent_dup, bool *relaxed) {
    if (relaxed) *relaxed = false;
    std::vector<int> pool;
    for (size_t c = 0; c < cats.size(); c++) {
        const std::vector<int> &v = lib.by_cat[cats[c]];
        for (size_t i = 0; i < v.size(); i++) {
            const Rec &r = lib.recs[v[i]];
            if (lib.rejects.count(r.path)) continue;
            if (prevent_dup && used.count(r.path)) continue;
            pool.push_back(v[i]);
        }
    }
    if (pad.has && !pool.empty()) {
        std::vector<int> alt;
        for (size_t i = 0; i < pool.size(); i++) if (lib.recs[pool[i]].path != pad.sample.path) alt.push_back(pool[i]);
        if (!alt.empty()) pool.swap(alt);
    }
    if (pool.empty() && prevent_dup) {
        std::vector<int> all;
        for (size_t c = 0; c < cats.size(); c++) {
            const std::vector<int> &v = lib.by_cat[cats[c]];
            for (size_t i = 0; i < v.size(); i++) if (!lib.rejects.count(lib.recs[v[i]].path)) all.push_back(v[i]);
        }
        if (!all.empty() && pad.has) {
            std::vector<int> alt;
            for (size_t i = 0; i < all.size(); i++) if (lib.recs[all[i]].path != pad.sample.path) alt.push_back(all[i]);
            if (!alt.empty()) all.swap(alt);
        }
        if (!all.empty()) { pool.swap(all); if (relaxed) *relaxed = true; }
    }
    if (pool.empty()) return -1;
    std::vector<int> weighted = pool;
    if (!lib.favourites.empty())
        for (size_t i = 0; i < pool.size(); i++) if (lib.favourites.count(lib.recs[pool[i]].path)) weighted.push_back(pool[i]);
    return weighted[(size_t)(rng.next() * weighted.size())];
}

struct AssignResult { int unresolved = 0, relaxed = 0; std::vector<int> changed; };

/* assignKit: every unlocked pad in ascending order. Locked samples count as used. */
inline AssignResult assign_kit(Pad pads[NPADS], const Library &lib, uint32_t seed, bool prevent_dup = true) {
    AssignResult res;
    Rng rng(seed);
    std::set<std::string> used;
    for (int i = 0; i < NPADS; i++) if (pads[i].locked && pads[i].has) used.insert(pads[i].sample.path);
    for (int i = 0; i < NPADS; i++) {
        Pad &p = pads[i];
        if (p.locked) continue;
        bool rel = false;
        int r = resolve_one(p, pool_cats(i, p.pool), lib, rng, used, prevent_dup, &rel);
        if (r < 0) { res.unresolved++; continue; }
        std::string prev = p.has ? p.sample.path : std::string();
        p.has = true; p.sample.path = lib.recs[r].path; p.sample.cat = lib.recs[r].cat;
        used.insert(p.sample.path);
        if (rel) res.relaxed++;
        if (p.sample.path != prev) res.changed.push_back(i);
    }
    return res;
}

/* rerollPad: one unlocked pad, avoiding every sample now in the kit and its own. Returns true on a new pick. */
inline bool reroll_pad(Pad pads[NPADS], int i, const Library &lib, uint32_t seed, bool prevent_dup = true) {
    if (i < 0 || i >= NPADS || pads[i].locked) return false;
    Rng rng(seed);
    std::set<std::string> used;
    for (int j = 0; j < NPADS; j++) if (j != i && pads[j].has) used.insert(pads[j].sample.path);
    bool rel;
    int r = resolve_one(pads[i], pool_cats(i, pads[i].pool), lib, rng, used, prevent_dup, &rel);
    if (r < 0) return false;
    bool changed = !pads[i].has || pads[i].sample.path != lib.recs[r].path;
    pads[i].has = true; pads[i].sample.path = lib.recs[r].path; pads[i].sample.cat = lib.recs[r].cat;
    return changed;
}

/* ---- loudness (loudness.mjs) ---------------------------------------------------------------------------------- */

/* matchGains: attenuate-only. Target = the 25th-percentile reading (the quietest pads keep unity gain, louder
 * ones are turned down to it); gain clamped to [0.15, 1.0] and rounded to 1e-3. rms <= 1e-5 = unmeasured (1.0). */
inline void match_gains(const float rms[NPADS], float gain[NPADS], float percentile = 0.25f, float min_gain = 0.15f,
                        float max_gain = 1.0f) {
    std::vector<float> nz;
    for (int i = 0; i < NPADS; i++) if (rms[i] > 1e-5f) nz.push_back(rms[i]);
    if (nz.empty()) { for (int i = 0; i < NPADS; i++) gain[i] = 1.0f; return; }
    std::sort(nz.begin(), nz.end());
    int idx = (int)(percentile * nz.size());
    idx = std::max(0, std::min((int)nz.size() - 1, idx));
    float target = nz[idx];
    for (int i = 0; i < NPADS; i++) {
        if (!(rms[i] > 1e-5f)) { gain[i] = 1.0f; continue; }
        float g = target / rms[i];
        if (g < min_gain) g = min_gain; else if (g > max_gain) g = max_gain;
        gain[i] = (float)(long)(g * 1000.0f + 0.5f) / 1000.0f;
    }
}

}  // namespace ld
