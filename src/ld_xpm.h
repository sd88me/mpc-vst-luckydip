/* Lucky Dip: Akai MPC .xpm drum-program export (exporters/mpc_xpm.mjs). Template-substitutes a real MPC-V 2.1
 * drum program (xpm_template.h): set <ProgramName>, emit all 128 <Instrument> blocks (kit pads 1-16 take Layer 1's
 * <SampleName>/<SliceEnd>), regenerate PadNoteMap/PadGroupMap, colour the 16 pads. Everything else stays
 * byte-for-byte. <SliceEnd> must be the sample's real frame count: SliceStart 0 + SliceEnd 0 is a zero-length
 * region and plays SILENCE on real hardware (confirmed on a Force) -- keep that in any rewrite. */
#pragma once
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "ld_audio.h"
#include "ld_core.h"
#include "xpm_template.h"

namespace ld {

static const size_t XPM_NAME_MAX = 42;
static const int XPM_INSTRUMENTS = 128;

inline std::string replace_first(std::string s, const std::string &from, const std::string &to) {
    size_t p = s.find(from);
    if (p != std::string::npos) s.replace(p, from.size(), to);
    return s;
}
inline std::string to_crlf(const std::string &s) {       /* collapse CRLF first, so it is safe to re-run */
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\r') continue;
        if (s[i] == '\n') o += '\r';
        o += s[i];
    }
    return o;
}
inline std::string xml_escape(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '&') o += "&amp;"; else if (s[i] == '<') o += "&lt;"; else if (s[i] == '>') o += "&gt;"; else o += s[i];
    }
    return o;
}
inline std::string base_name(const std::string &p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}
inline std::string file_ext(const std::string &p) {       /* lower-case ".wav" etc; ".wav" when there is none */
    size_t d = p.find_last_of('.'), s = p.find_last_of('/');
    if (d == std::string::npos || (s != std::string::npos && d < s)) return ".wav";
    return lower(p.substr(d));
}

/* the file's basename, no extension, filesystem/XML-safe (spaces and parens kept), capped at 42 */
inline std::string mpc_sample_name(const std::string &path) {
    std::string b = base_name(path);
    size_t d = b.find_last_of('.');
    if (d != std::string::npos && d > 0) b = b.substr(0, d);
    std::string t;
    bool in_bad = false, in_ws = false;
    for (size_t i = 0; i < b.size(); i++) {
        unsigned char c = (unsigned char)b[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '(' ||
                  c == ')' || c == '_' || c == '-';
        if (!ok) { if (!in_bad) t += '_'; in_bad = true; in_ws = false; continue; }
        in_bad = false;
        if (c == ' ') { if (in_ws) continue; in_ws = true; } else in_ws = false;
        t += (char)c;
    }
    size_t a = 0, e = t.size();
    while (a < e && (t[a] == ' ' || t[a] == '_' || t[a] == '-')) a++;
    while (e > a && (t[e - 1] == ' ' || t[e - 1] == '_' || t[e - 1] == '-')) e--;
    t = t.substr(a, e - a);
    if (t.empty()) t = "sample";
    if (t.size() > XPM_NAME_MAX) {
        t = t.substr(0, XPM_NAME_MAX);
        size_t k = t.size();
        while (k > 0 && (t[k - 1] == ' ' || t[k - 1] == '_' || t[k - 1] == '-')) k--;
        t.resize(k);
    }
    return t;
}

struct XpmEntry { int pad; std::string sample_name, src, ext, dest; uint32_t frames; bool gathered; };

/* <ProgramPads>: pads.value0..15 are pad LED colours as decimal RGB. Only those 16 slots inside the "pads"
 * block are touched (the JS original replaced the FIRST `value0: <digits>` in the file, which is the "Type"
 * block's `value0: 2`, so pad 1's colour overwrote the program type there). */
inline std::string pad_colours(const Pad pads[NPADS]) {
    std::string s = XPM_PROGPADS;
    size_t from = s.find("&quot;pads&quot;");
    if (from == std::string::npos) return s;
    for (int i = 0; i < NPADS; i++) {
        if (!pads[i].has) continue;
        char key[48];
        snprintf(key, sizeof key, "&quot;value%d&quot;: ", i);
        size_t p = s.find(key, from);
        if (p == std::string::npos) continue;
        size_t v = p + strlen(key), e = v;
        while (e < s.size() && s[e] >= '0' && s[e] <= '9') e++;
        if (e == v) continue;
        char num[16];
        snprintf(num, sizeof num, "%u", (unsigned)CAT_COLOR[pads[i].sample.cat]);
        s.replace(v, e - v, num);
    }
    return s;
}

inline std::string instrument_block(int n, const std::string &name, uint32_t slice_end) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%d", n);
    std::string b = replace_first(XPM_INSTRUMENT, "<Instrument number=\"1\">", std::string("<Instrument number=\"") + tmp + "\">");
    b = replace_first(b, "<SampleName>PRGKIT26BD1</SampleName>", "<SampleName>" + xml_escape(name) + "</SampleName>");
    snprintf(tmp, sizeof tmp, "%u", (unsigned)slice_end);
    b = replace_first(b, "<SliceEnd>33688</SliceEnd>", std::string("<SliceEnd>") + tmp + "</SliceEnd>");
    if (name.empty()) {        /* an empty pad differs from a used one by two inert defaults in a real MPC export */
        b = replace_first(b, "<WarpTempo>20.000000</WarpTempo>", "<WarpTempo>120.000000</WarpTempo>");
        b = replace_first(b, "<SliceLoopCrossFadeLength>0</SliceLoopCrossFadeLength>", "<SliceLoopCrossFadeLength>-1</SliceLoopCrossFadeLength>");
    }
    return b;
}

/* -> the .xpm text; `manifest` lists the used pads (a name clash gets the pad number appended) */
inline std::string build_xpm(const std::string &kit_name, const Pad pads[NPADS], std::vector<XpmEntry> &manifest,
                             std::vector<std::string> &warnings) {
    std::set<std::string> seen;
    std::string instrs, nm;
    for (int n = 1; n <= XPM_INSTRUMENTS; n++) {
        std::string sn;
        uint32_t se = 0;
        if (n <= NPADS && pads[n - 1].has) {
            const std::string &src = pads[n - 1].sample.path;
            sn = mpc_sample_name(src);
            if (seen.count(sn)) { char t[16]; snprintf(t, sizeof t, " %d", n); sn = (sn + t).substr(0, XPM_NAME_MAX); }
            seen.insert(sn);
            XpmEntry e;
            e.pad = n; e.sample_name = sn; e.src = src; e.ext = file_ext(src); e.dest = sn + e.ext; e.gathered = false;
            e.frames = frame_count(src);
            if (e.frames > 0) se = e.frames;
            else warnings.push_back("pad " + std::to_string(n) + ": could not read sample length (may play silent)");
            manifest.push_back(e);
        }
        instrs += instrument_block(n, sn, se);
    }
    std::string pnm = "    <PadNoteMap>\n", pgm = "    <PadGroupMap>\n";
    for (int i = 1; i <= XPM_INSTRUMENTS; i++) {
        pnm += "      <PadNote number=\"" + std::to_string(i) + "\">\n        <Note>" + std::to_string((35 + i) % 128) + "</Note>\n      </PadNote>\n";
        pgm += "      <PadGroup number=\"" + std::to_string(i) + "\">\n        <Group>0</Group>\n      </PadGroup>\n";
    }
    pnm += "    </PadNoteMap>\n"; pgm += "    </PadGroupMap>\n";
    std::string prog = "    <ProgramName>" + xml_escape(kit_name) + "</ProgramName>\n";
    return to_crlf(std::string(XPM_HEAD) + prog) + pad_colours(pads) +
           to_crlf(std::string(XPM_PROG_PARAMS) + instrs + "    </Instruments>\n" + pnm + pgm) + to_crlf(XPM_TAIL);
}

inline bool copy_file(const std::string &from, const std::string &to) {
    FILE *a = fopen(from.c_str(), "rb");
    if (!a) return false;
    std::string tmp = to + ".part";
    FILE *b = fopen(tmp.c_str(), "wb");
    if (!b) { fclose(a); return false; }
    char buf[64 * 1024];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) if (fwrite(buf, 1, n, b) != n) { ok = false; break; }
    fclose(a);
    if (fclose(b) != 0) ok = false;
    if (!ok || rename(tmp.c_str(), to.c_str()) != 0) { remove(tmp.c_str()); return false; }
    return true;
}
inline bool write_text(const std::string &path, const std::string &text) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    return fclose(f) == 0 && ok;
}
/* mkdir -p */
inline bool make_dirs(const std::string &path) {
    std::string cur;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty() && mkdir(cur.c_str(), 0777) != 0 && errno != EEXIST) return false;
        }
        if (i < path.size()) cur += path[i];
    }
    struct stat s;
    return stat(path.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
}

struct ExportResult { bool ok = false; std::string path, error; int pads = 0, gathered = 0; std::vector<std::string> warnings; };

/* exportXpm: <dir>/<name>/<name>.xpm + each sample copied beside it (named to match <SampleName>) + MANIFEST.txt.
 * Samples are byte-exact copies; the MPC loads "<SampleName>.wav" from the .xpm's own folder. */
inline ExportResult export_xpm(const std::string &dir, const std::string &name_in, const Pad pads[NPADS]) {
    ExportResult r;
    std::string name = name_in;
    for (size_t i = 0; i < name.size(); i++) if (name[i] == '/' || name[i] == '\\') name[i] = '_';
    std::vector<XpmEntry> man;
    std::string text = build_xpm(name, pads, man, r.warnings);
    r.pads = (int)man.size();
    if (man.empty()) { r.error = "kit has no assigned pads"; return r; }
    std::string d = dir + "/" + name;
    if (!make_dirs(d)) { r.error = "cannot create " + d; return r; }
    r.path = d + "/" + name + ".xpm";
    if (!write_text(r.path, text)) { r.error = "xpm write failed"; return r; }
    std::string mf = "Lucky Dip MPC export: the files below were copied next to this .xpm.\n"
                     "Any row tagged [MISSING] must be placed by hand (keep the left-hand name).\n\n";
    for (size_t i = 0; i < man.size(); i++) {
        XpmEntry &m = man[i];
        if (m.ext != ".wav") r.warnings.push_back("pad " + std::to_string(m.pad) + ": source is " + m.ext + "; the MPC loads " + m.sample_name + ".wav beside the .xpm");
        m.gathered = copy_file(m.src, d + "/" + m.dest);
        if (m.gathered) r.gathered++;
        else r.warnings.push_back("pad " + std::to_string(m.pad) + ": could not copy " + m.src);
        mf += m.dest + "\t" + m.src + (m.gathered ? "\t[gathered]\n" : "\t[MISSING - copy by hand]\n");
    }
    write_text(d + "/MANIFEST.txt", mf);
    r.ok = true;
    return r;
}

}  // namespace ld
