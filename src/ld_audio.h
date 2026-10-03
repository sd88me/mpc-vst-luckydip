/* Lucky Dip audio helpers: WAV/AIFF header walk (frame count, as the XPM export needs), decode to
 * 44.1 kHz int16 stereo for the pad voices, and RMS (wav_info.mjs, wav_rms.mjs). No host code. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace ld {

struct Pcm {                       /* a decoded sample, ready to play */
    std::vector<int16_t> lr;       /* interleaved stereo, 44100 Hz */
    float rms = 0;                 /* whole-file RMS as a fraction of full scale (all channels) */
    uint32_t src_frames = 0;       /* frames in the source file (the XPM SliceEnd) */
    bool truncated = false;
    uint32_t frames() const { return (uint32_t)(lr.size() / 2); }
};

static const uint32_t OUT_RATE = 44100;
static const uint32_t MAX_FRAMES = OUT_RATE * 15;       /* a pad is a one-shot: cap what we hold per pad */
static const long MAX_FILE_BYTES = 64L * 1024 * 1024;   /* never slurp more than this for one pad */

inline uint32_t rd_le32(const uint8_t *b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
inline uint16_t rd_le16(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
inline uint32_t rd_be32(const uint8_t *b) { return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }
inline uint16_t rd_be16(const uint8_t *b) { return (uint16_t)((b[0] << 8) | b[1]); }

struct Fmt {
    int format = 0;        /* 1 = integer PCM, 3 = float */
    int channels = 0, bits = 0;
    uint32_t rate = 0, frames = 0;
    size_t data_off = 0, data_size = 0;
    bool big_endian = false;
};

/* RIFF/WAVE chunk walk. Understands WAVE_FORMAT_EXTENSIBLE (real sample packs use it). */
inline bool parse_wav(const std::vector<uint8_t> &b, Fmt &f) {
    if (b.size() < 12 || memcmp(&b[0], "RIFF", 4) || memcmp(&b[8], "WAVE", 4)) return false;
    size_t o = 12;
    bool have_fmt = false, have_data = false;
    while (o + 8 <= b.size()) {
        uint32_t sz = rd_le32(&b[o + 4]);
        size_t body = o + 8;
        if (!memcmp(&b[o], "fmt ", 4) && sz >= 16 && body + 16 <= b.size()) {
            f.format = rd_le16(&b[body]);
            f.channels = rd_le16(&b[body + 2]);
            f.rate = rd_le32(&b[body + 4]);
            f.bits = rd_le16(&b[body + 14]);
            if (f.format == 0xFFFE && sz >= 26 && body + 26 <= b.size()) f.format = rd_le16(&b[body + 24]);
            have_fmt = true;
        } else if (!memcmp(&b[o], "data", 4)) {
            f.data_off = body;
            f.data_size = body + sz > b.size() ? b.size() - body : sz;   /* tolerate a truncated final chunk */
            have_data = true;
        }
        if (have_fmt && have_data) break;
        if ((uint64_t)body + sz > b.size()) break;
        o = body + sz + (sz & 1);
    }
    if (!have_fmt || !have_data || f.channels < 1 || f.bits < 8) return false;
    int ba = f.channels * (f.bits >> 3);
    f.frames = ba > 0 ? (uint32_t)(f.data_size / ba) : 0;
    return true;
}

/* IEEE 80-bit extended -> Hz (the AIFF COMM sample rate) */
inline double ext80(const uint8_t *p) {
    int exp = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mant = 0;
    for (int i = 0; i < 8; i++) mant = (mant << 8) | p[2 + i];
    if (!exp && !mant) return 0;
    return ldexp((double)mant, exp - 16383 - 63);
}

inline bool parse_aiff(const std::vector<uint8_t> &b, Fmt &f) {
    if (b.size() < 12 || memcmp(&b[0], "FORM", 4) || memcmp(&b[8], "AIFF", 4)) return false;   /* not AIFC */
    size_t o = 12;
    bool have_fmt = false, have_data = false;
    while (o + 8 <= b.size()) {
        uint32_t sz = rd_be32(&b[o + 4]);
        size_t body = o + 8;
        if (!memcmp(&b[o], "COMM", 4) && sz >= 18 && body + 18 <= b.size()) {
            f.channels = rd_be16(&b[body]);
            f.frames = rd_be32(&b[body + 2]);
            f.bits = rd_be16(&b[body + 6]);
            f.rate = (uint32_t)(ext80(&b[body + 8]) + 0.5);
            f.format = 1; f.big_endian = true;
            have_fmt = true;
        } else if (!memcmp(&b[o], "SSND", 4) && sz >= 8 && body + 8 <= b.size()) {
            uint32_t off = rd_be32(&b[body]);
            f.data_off = body + 8 + off;
            f.data_size = body + sz > b.size() ? b.size() - body - 8 - off : sz - 8 - off;
            have_data = f.data_off <= b.size();
        }
        if (have_fmt && have_data) break;
        if ((uint64_t)body + sz > b.size()) break;
        o = body + sz + (sz & 1);
    }
    if (!have_fmt || !have_data || f.channels < 1 || f.bits < 8) return false;
    int ba = f.channels * ((f.bits + 7) >> 3);
    f.frames = (uint32_t)(f.data_size / ba) < f.frames ? (uint32_t)(f.data_size / ba) : f.frames;
    return true;
}

inline bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 0 || n > MAX_FILE_BYTES) { fclose(fp); return false; }
    out.resize((size_t)n);
    size_t got = fread(&out[0], 1, (size_t)n, fp);
    fclose(fp);
    return got == (size_t)n;
}

/* Frame `i`, channel `c` as a float in -1..1; false for a shape we don't read. */
inline bool sample_at(const std::vector<uint8_t> &b, const Fmt &f, uint32_t i, int c, float &v) {
    int bytes = (f.bits + 7) >> 3;
    size_t o = f.data_off + ((size_t)i * f.channels + c) * bytes;
    if (o + bytes > b.size()) return false;
    const uint8_t *p = &b[o];
    if (f.format == 3 && f.bits == 32 && !f.big_endian) { uint32_t u = rd_le32(p); float x; memcpy(&x, &u, 4); v = x; return true; }
    if (f.format != 1) return false;
    int32_t s;
    if (f.big_endian) {
        switch (bytes) {
            case 1: v = (int8_t)p[0] / 128.0f; return true;
            case 2: s = (int16_t)rd_be16(p); v = s / 32768.0f; return true;
            case 3: s = (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8)) >> 8; v = s / 8388608.0f; return true;
            case 4: s = (int32_t)rd_be32(p); v = s / 2147483648.0f; return true;
        }
        return false;
    }
    switch (bytes) {
        case 1: v = ((int)p[0] - 128) / 128.0f; return true;                      /* 8-bit WAV is unsigned */
        case 2: s = (int16_t)rd_le16(p); v = s / 32768.0f; return true;
        case 3: s = (int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24)) >> 8; v = s / 8388608.0f; return true;
        case 4: s = (int32_t)rd_le32(p); v = s / 2147483648.0f; return true;
    }
    return false;
}

inline bool parse_any(const std::vector<uint8_t> &b, Fmt &f) { return parse_wav(b, f) || parse_aiff(b, f); }

/* Source frame count from the header alone (the XPM's SliceEnd: 0 plays silence on real hardware). 0 = unreadable. */
inline uint32_t frame_count(const std::string &path) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;
    std::vector<uint8_t> b(64 * 1024);
    size_t n = fread(&b[0], 1, b.size(), fp);
    b.resize(n);
    /* the data chunk's size field may sit past the first 64 KiB in a file with big metadata: fall back to a full read */
    Fmt f;
    bool ok = parse_any(b, f) && !(f.data_off + f.data_size > b.size() && n == 64 * 1024);
    fclose(fp);
    if (!ok) {
        std::vector<uint8_t> full;
        if (!read_file(path, full) || !parse_any(full, f)) return 0;
    }
    return f.frames;
}

/* Decode to 44.1 kHz int16 stereo (mono is duplicated, >2 channels keep the first two, other rates are
 * linearly resampled) and measure the RMS over the source. At most MAX_FRAMES are kept. */
inline bool decode(const std::string &path, Pcm &out) {
    std::vector<uint8_t> b;
    Fmt f;
    if (!read_file(path, b) || !parse_any(b, f) || f.frames == 0 || f.rate < 1000 || f.rate > 384000) return false;
    float probe;
    if (!sample_at(b, f, 0, 0, probe)) return false;       /* an unsupported shape (compressed WAV, 64-bit...) */
    out.src_frames = f.frames;
    double sumsq = 0; uint64_t n = 0;
    for (uint32_t i = 0; i < f.frames; i++)
        for (int c = 0; c < f.channels; c++) { float v; if (sample_at(b, f, i, c, v)) { sumsq += (double)v * v; n++; } }
    out.rms = n ? (float)sqrt(sumsq / n) : 0.0f;
    uint64_t outn = (uint64_t)((double)f.frames * OUT_RATE / f.rate);
    if (outn < 1) outn = 1;
    if (outn > MAX_FRAMES) { outn = MAX_FRAMES; out.truncated = true; }
    out.lr.assign((size_t)outn * 2, 0);
    double step = (double)f.rate / OUT_RATE;
    for (uint64_t k = 0; k < outn; k++) {
        double pos = k * step;
        uint32_t i0 = (uint32_t)pos, i1 = i0 + 1 < f.frames ? i0 + 1 : i0;
        float fr = (float)(pos - i0);
        float ch[2];
        for (int c = 0; c < 2; c++) {
            int src = f.channels == 1 ? 0 : c;
            float a = 0, bb = 0;
            sample_at(b, f, i0, src, a); sample_at(b, f, i1, src, bb);
            ch[c] = a + (bb - a) * fr;
        }
        for (int c = 0; c < 2; c++) {
            float x = ch[c] * 32767.0f;
            out.lr[(size_t)k * 2 + c] = (int16_t)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : lrintf(x));
        }
    }
    return true;
}

}  // namespace ld
