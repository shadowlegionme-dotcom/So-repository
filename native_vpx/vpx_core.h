#pragma once
// WebM (VP8/VP9, alpha shoho) -> RGBA decoder. JNI chhara, tai host-e test kora jay.
// Parser nijer lekha (sticker file chhoto, puro file memory te), decoder = libvpx.
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace vpxw {

struct Frame {
    int64_t ptsMs;
    size_t off, size;    // color frame
    size_t aoff, asize;  // alpha frame (VP9 gray), asize==0 = nai
};

struct Webm {
    std::vector<uint8_t> data;
    std::vector<Frame> frames;
    int width = 0, height = 0;
    bool vp8 = false;
    bool alphaMode = false;
    int64_t tcScaleNs = 1000000;
    int64_t trackNo = -1;
    vpx_codec_ctx_t dec, decA;
    bool decInit = false, decAInit = false;
    int next = 0;  // porer decode howa frame index
    int64_t durationMs = 0;
};

// ---------------- EBML parsing ----------------
static const uint32_t ID_SEGMENT = 0x18538067, ID_INFO = 0x1549A966, ID_TCSCALE = 0x2AD7B1,
                      ID_TRACKS = 0x1654AE6B, ID_TRACKENTRY = 0xAE, ID_TRACKNO = 0xD7,
                      ID_TRACKTYPE = 0x83, ID_CODEC = 0x86, ID_VIDEO = 0xE0, ID_PW = 0xB0,
                      ID_PH = 0xBA, ID_ALPHAMODE = 0x53C0, ID_CLUSTER = 0x1F43B675,
                      ID_TC = 0xE7, ID_SIMPLEBLOCK = 0xA3, ID_BLOCKGROUP = 0xA0, ID_BLOCK = 0xA1,
                      ID_ADDITIONS = 0x75A1, ID_MORE = 0xA6, ID_ADDITIONAL = 0xA5, ID_ADDID = 0xEE;

struct Reader {
    const uint8_t *p;
    size_t n;
};

// element ID (marker bit shoho rekhe), return length or 0 on error
static inline int readId(const Reader &r, size_t pos, uint32_t *id) {
    if (pos >= r.n) return 0;
    uint8_t b = r.p[pos];
    int len = 1;
    uint8_t mask = 0x80;
    while (len <= 4 && !(b & mask)) {
        len++;
        mask >>= 1;
    }
    if (len > 4 || pos + len > r.n) return 0;
    uint32_t v = 0;
    for (int i = 0; i < len; i++) v = (v << 8) | r.p[pos + i];
    *id = v;
    return len;
}

// size/vint (marker bit baad diye). unknown = all ones
static inline int readVint(const Reader &r, size_t pos, uint64_t *val, bool *unknown) {
    if (pos >= r.n) return 0;
    uint8_t b = r.p[pos];
    int len = 1;
    uint8_t mask = 0x80;
    while (len <= 8 && !(b & mask)) {
        len++;
        mask >>= 1;
    }
    if (len > 8 || pos + len > r.n) return 0;
    uint64_t v = b & (mask - 1);
    bool allOnes = (v == (uint64_t)(mask - 1));
    for (int i = 1; i < len; i++) {
        v = (v << 8) | r.p[pos + i];
        if (r.p[pos + i] != 0xFF) allOnes = false;
    }
    *val = v;
    if (unknown) *unknown = allOnes;
    return len;
}

static inline uint64_t readUInt(const Reader &r, size_t pos, uint64_t size) {
    uint64_t v = 0;
    for (uint64_t i = 0; i < size && pos + i < r.n; i++) v = (v << 8) | r.p[pos + i];
    return v;
}

struct Parser {
    Webm &w;
    Reader r;
    int64_t clusterTc = 0;
    bool videoSeen = false;

    explicit Parser(Webm &w_) : w(w_) {
        r.p = w.data.data();
        r.n = w.data.size();
    }

    void handleBlock(size_t pos, size_t size, bool group, size_t aoff, size_t asize) {
        // track vint, int16 timecode, flags
        uint64_t trk;
        int l = readVint(r, pos, &trk, nullptr);
        if (!l || size < (size_t)l + 3) return;
        if (w.trackNo >= 0 && (int64_t)trk != w.trackNo) return;
        int16_t rel = (int16_t)((r.p[pos + l] << 8) | r.p[pos + l + 1]);
        uint8_t flags = r.p[pos + l + 2];
        if (flags & 0x06) return;  // lacing support kori na (video te hoy na)
        Frame f;
        f.ptsMs = ((clusterTc + rel) * w.tcScaleNs) / 1000000;
        f.off = pos + l + 3;
        f.size = size - (l + 3);
        f.aoff = aoff;
        f.asize = asize;
        (void)group;
        w.frames.push_back(f);
    }

    // children of element in [pos,end). returns false on fatal error
    bool parseChildren(size_t pos, size_t end, uint32_t parent, int depth) {
        if (depth > 8) return false;
        while (pos < end) {
            uint32_t id;
            int il = readId(r, pos, &id);
            if (!il) return false;
            uint64_t size;
            bool unknown = false;
            int sl = readVint(r, pos + il, &size, &unknown);
            if (!sl) return false;
            size_t body = pos + il + sl;
            size_t elEnd = unknown ? end : body + (size_t)size;
            if (elEnd > end || elEnd < body) elEnd = end;  // truncated file: jotodur ache

            if (parent == ID_CLUSTER && (id == ID_CLUSTER || id == ID_SEGMENT)) return true;  // unknown-size cluster sesh

            switch (id) {
                case ID_SEGMENT:
                case ID_TRACKS:
                case ID_TRACKENTRY:
                case ID_VIDEO:
                case ID_INFO:
                    if (id == ID_TRACKENTRY) {
                        // ekta track er field gulo alada kore porbo
                        if (!parseTrackEntry(body, elEnd)) return false;
                    } else if (!parseChildren(body, elEnd, id, depth + 1)) {
                        return false;
                    }
                    break;
                case ID_TCSCALE:
                    w.tcScaleNs = (int64_t)readUInt(r, body, size);
                    break;
                case ID_CLUSTER: {
                    clusterTc = 0;
                    // unknown-size cluster: porer Cluster er age porjonto
                    size_t cEnd = elEnd;
                    if (!parseClusterBody(body, cEnd)) return false;
                    if (unknown) {
                        // parseClusterBody ta jekhane thamse (next cluster) shekhan theke cholbe
                        pos = clusterStop;
                        continue;
                    }
                    break;
                }
                default:
                    break;
            }
            pos = elEnd;
        }
        return true;
    }

    size_t clusterStop = 0;

    bool parseClusterBody(size_t pos, size_t end) {
        while (pos < end) {
            uint32_t id;
            int il = readId(r, pos, &id);
            if (!il) return false;
            uint64_t size;
            bool unknown = false;
            int sl = readVint(r, pos + il, &size, &unknown);
            if (!sl) return false;
            size_t body = pos + il + sl;
            size_t elEnd = unknown ? end : body + (size_t)size;
            if (elEnd > end || elEnd < body) elEnd = end;
            if (id == ID_CLUSTER || id == ID_SEGMENT) {
                clusterStop = pos;
                return true;
            }
            if (id == ID_TC) {
                clusterTc = (int64_t)readUInt(r, body, size);
            } else if (id == ID_SIMPLEBLOCK) {
                handleBlock(body, elEnd - body, false, 0, 0);
            } else if (id == ID_BLOCKGROUP) {
                parseBlockGroup(body, elEnd);
            }
            pos = elEnd;
        }
        clusterStop = end;
        return true;
    }

    void parseBlockGroup(size_t pos, size_t end) {
        size_t bpos = 0, bsize = 0, aoff = 0, asize = 0;
        while (pos < end) {
            uint32_t id;
            int il = readId(r, pos, &id);
            if (!il) break;
            uint64_t size;
            int sl = readVint(r, pos + il, &size, nullptr);
            if (!sl) break;
            size_t body = pos + il + sl;
            size_t elEnd = body + (size_t)size;
            if (elEnd > end || elEnd < body) elEnd = end;
            if (id == ID_BLOCK) {
                bpos = body;
                bsize = elEnd - body;
            } else if (id == ID_ADDITIONS) {
                parseAdditions(body, elEnd, &aoff, &asize);
            }
            pos = elEnd;
        }
        if (bsize) handleBlock(bpos, bsize, true, aoff, asize);
    }

    void parseAdditions(size_t pos, size_t end, size_t *aoff, size_t *asize) {
        while (pos < end) {
            uint32_t id;
            int il = readId(r, pos, &id);
            if (!il) return;
            uint64_t size;
            int sl = readVint(r, pos + il, &size, nullptr);
            if (!sl) return;
            size_t body = pos + il + sl;
            size_t elEnd = body + (size_t)size;
            if (elEnd > end || elEnd < body) elEnd = end;
            if (id == ID_MORE) {
                uint64_t addId = 1;
                size_t dOff = 0, dSize = 0;
                size_t q = body;
                while (q < elEnd) {
                    uint32_t cid;
                    int cl = readId(r, q, &cid);
                    if (!cl) break;
                    uint64_t cs;
                    int csl = readVint(r, q + cl, &cs, nullptr);
                    if (!csl) break;
                    size_t cb = q + cl + csl;
                    size_t ce = cb + (size_t)cs;
                    if (ce > elEnd || ce < cb) ce = elEnd;
                    if (cid == ID_ADDID) addId = readUInt(r, cb, cs);
                    if (cid == ID_ADDITIONAL) {
                        dOff = cb;
                        dSize = ce - cb;
                    }
                    q = ce;
                }
                if (addId == 1 && dSize) {
                    *aoff = dOff;
                    *asize = dSize;
                }
            }
            pos = elEnd;
        }
    }

    bool parseTrackEntry(size_t pos, size_t end) {
        int64_t no = -1, type = -1;
        std::string codec;
        int pw = 0, ph = 0;
        bool alpha = false;
        while (pos < end) {
            uint32_t id;
            int il = readId(r, pos, &id);
            if (!il) return false;
            uint64_t size;
            int sl = readVint(r, pos + il, &size, nullptr);
            if (!sl) return false;
            size_t body = pos + il + sl;
            size_t elEnd = body + (size_t)size;
            if (elEnd > end || elEnd < body) elEnd = end;
            if (id == ID_TRACKNO) no = (int64_t)readUInt(r, body, size);
            else if (id == ID_TRACKTYPE) type = (int64_t)readUInt(r, body, size);
            else if (id == ID_CODEC) codec.assign((const char *)r.p + body, elEnd - body);
            else if (id == ID_VIDEO) {
                size_t q = body;
                while (q < elEnd) {
                    uint32_t cid;
                    int cl = readId(r, q, &cid);
                    if (!cl) break;
                    uint64_t cs;
                    int csl = readVint(r, q + cl, &cs, nullptr);
                    if (!csl) break;
                    size_t cb = q + cl + csl;
                    size_t ce = cb + (size_t)cs;
                    if (ce > elEnd || ce < cb) ce = elEnd;
                    if (cid == ID_PW) pw = (int)readUInt(r, cb, cs);
                    else if (cid == ID_PH) ph = (int)readUInt(r, cb, cs);
                    else if (cid == ID_ALPHAMODE) alpha = readUInt(r, cb, cs) == 1;
                    q = ce;
                }
            }
            pos = elEnd;
        }
        if (type == 1 && !videoSeen && (codec == "V_VP9" || codec == "V_VP8")) {
            videoSeen = true;
            w.trackNo = no;
            w.vp8 = codec == "V_VP8";
            w.width = pw;
            w.height = ph;
            w.alphaMode = alpha;
        }
        return true;
    }
};

// ---------------- decode ----------------
static inline void close(Webm *w) {
    if (!w) return;
    if (w->decInit) vpx_codec_destroy(&w->dec);
    if (w->decAInit) vpx_codec_destroy(&w->decA);
    delete w;
}

static inline bool initDecoders(Webm *w) {
    vpx_codec_dec_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.threads = 1;
    cfg.w = (unsigned)w->width;
    cfg.h = (unsigned)w->height;
    vpx_codec_iface_t *iface = w->vp8 ? vpx_codec_vp8_dx() : vpx_codec_vp9_dx();
    if (vpx_codec_dec_init(&w->dec, iface, &cfg, 0)) return false;
    w->decInit = true;
    if (w->alphaMode && !w->vp8) {
        if (vpx_codec_dec_init(&w->decA, vpx_codec_vp9_dx(), &cfg, 0)) return false;
        w->decAInit = true;
    }
    return true;
}

// buf copy kore nei. null = unsupported/bhanga file
static inline Webm *open(const uint8_t *buf, size_t n) {
    Webm *w = new Webm();
    w->data.assign(buf, buf + n);
    Parser ps(*w);
    ps.parseChildren(0, w->data.size(), 0, 0);  // truncated hole-o jotodur pawa jay
    if (!ps.videoSeen || w->frames.empty() || w->width <= 0 || w->height <= 0 ||
        w->width > 4096 || w->height > 4096 || w->frames.size() > 20000) {
        delete w;
        return nullptr;
    }
    // frame er range check (bhanga file e out-of-bounds e jeno na jay)
    std::vector<Frame> ok;
    for (size_t i = 0; i < w->frames.size(); i++) {
        const Frame &f = w->frames[i];
        if (f.size == 0 || f.off + f.size > w->data.size()) continue;
        Frame g = f;
        if (g.asize && g.aoff + g.asize > w->data.size()) g.asize = 0;
        ok.push_back(g);
    }
    w->frames.swap(ok);
    if (w->frames.empty() || !initDecoders(w)) {
        close(w);
        return nullptr;
    }
    int64_t last = w->frames.back().ptsMs;
    size_t cnt = w->frames.size();
    w->durationMs = last + (cnt > 1 ? last / (int64_t)(cnt - 1) : 33);
    if (w->durationMs <= 0) w->durationMs = 33 * (int64_t)cnt;
    return w;
}

static inline bool decodeRaw(Webm *w, int idx, const vpx_image_t **img, const vpx_image_t **aimg) {
    const Frame &f = w->frames[idx];
    *img = nullptr;
    *aimg = nullptr;
    if (vpx_codec_decode(&w->dec, w->data.data() + f.off, (unsigned)f.size, nullptr, 0)) return false;
    vpx_codec_iter_t it = nullptr;
    *img = vpx_codec_get_frame(&w->dec, &it);
    if (w->decAInit && f.asize) {
        if (vpx_codec_decode(&w->decA, w->data.data() + f.aoff, (unsigned)f.asize, nullptr, 0)) return false;
        vpx_codec_iter_t it2 = nullptr;
        *aimg = vpx_codec_get_frame(&w->decA, &it2);
    }
    return *img != nullptr;
}

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// RGBA_8888, alpha premultiplied (Android Bitmap er moto). W x H te nearest sampling.
static inline bool convert(const vpx_image_t *img, const vpx_image_t *aimg, uint8_t *out, int W, int H,
                           size_t stride) {
    if (!img || W <= 0 || H <= 0) return false;
    if (img->fmt & VPX_IMG_FMT_HIGHBITDEPTH) return false;
    if (!img->planes[0] || !img->planes[1] || !img->planes[2]) return false;
    const int sw = (int)img->d_w, sh = (int)img->d_h;
    if (sw <= 0 || sh <= 0) return false;
    const int xs = img->x_chroma_shift, ys = img->y_chroma_shift;
    const bool bt709 = img->cs == VPX_CS_BT_709;
    const bool full = img->range == VPX_CR_FULL_RANGE;
    int kR, kGu, kGv, kB, yoff, ymul;
    if (full) {
        ymul = 256; yoff = 0;
        if (bt709) { kR = 403; kGu = 48; kGv = 120; kB = 475; }
        else       { kR = 359; kGu = 88; kGv = 183; kB = 454; }
    } else {
        ymul = 298; yoff = 16;
        if (bt709) { kR = 459; kGu = 55; kGv = 136; kB = 541; }
        else       { kR = 409; kGu = 100; kGv = 208; kB = 516; }
    }
    std::vector<int> sx((size_t)W);
    for (int x = 0; x < W; x++) sx[(size_t)x] = (int)((int64_t)x * sw / W);
    const bool hasA = aimg && aimg->planes[0] && (int)aimg->d_w >= sw && (int)aimg->d_h >= sh;
    for (int y = 0; y < H; y++) {
        int syy = (int)((int64_t)y * sh / H);
        const uint8_t *yr = img->planes[0] + (size_t)syy * img->stride[0];
        const uint8_t *ur = img->planes[1] + (size_t)(syy >> ys) * img->stride[1];
        const uint8_t *vr = img->planes[2] + (size_t)(syy >> ys) * img->stride[2];
        const uint8_t *ar = hasA ? aimg->planes[0] + (size_t)syy * aimg->stride[0] : nullptr;
        uint8_t *o = out + (size_t)y * stride;
        for (int x = 0; x < W; x++) {
            int xx = sx[(size_t)x];
            int c = (int)yr[xx] - yoff;
            if (c < 0) c = 0;
            int d = (int)ur[xx >> xs] - 128;
            int e = (int)vr[xx >> xs] - 128;
            int r = (ymul * c + kR * e + 128) >> 8;
            int g = (ymul * c - kGu * d - kGv * e + 128) >> 8;
            int b = (ymul * c + kB * d + 128) >> 8;
            int a = ar ? ar[xx] : 255;
            if (a != 255) {
                r = (clamp8(r) * a + 127) / 255;
                g = (clamp8(g) * a + 127) / 255;
                b = (clamp8(b) * a + 127) / 255;
            }
            o[0] = clamp8(r);
            o[1] = clamp8(g);
            o[2] = clamp8(b);
            o[3] = (uint8_t)a;
            o += 4;
        }
    }
    return true;
}

// idx-tho frame decode kore (rgba != null hole) convert kore. Decode sequential, tai pichone
// gele 0 theke abar decode kore. Return false = error.
static inline bool decodeFrame(Webm *w, int idx, uint8_t *rgba, int W, int H, size_t stride) {
    if (!w || idx < 0 || idx >= (int)w->frames.size()) return false;
    const vpx_image_t *img = nullptr, *aimg = nullptr;
    if (idx < w->next) w->next = 0;  // rewind (frame 0 key frame)
    while (w->next < idx) {
        decodeRaw(w, w->next, &img, &aimg);  // error hole-o chaliye jai
        w->next++;
    }
    bool ok = decodeRaw(w, idx, &img, &aimg);
    w->next = idx + 1;
    if (!ok) return false;
    if (!rgba) return true;
    return convert(img, aimg, rgba, W, H, stride);
}

}  // namespace vpxw
