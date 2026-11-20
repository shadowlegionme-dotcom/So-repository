#pragma once
// Platform-independent part (JNI chhara), jate host-e-o test kora jay.
#include <rlottie.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

struct LottieHandle {
    std::unique_ptr<rlottie::Animation> anim;
    size_t width = 0, height = 0, frames = 0;
    double fps = 30.0;
};

static inline LottieHandle *lottieCreate(const char *json, const char *key) {
    if (!json) return nullptr;
    std::unique_ptr<rlottie::Animation> a =
        rlottie::Animation::loadFromData(std::string(json), std::string(key ? key : ""));
    if (!a) return nullptr;
    LottieHandle *h = new LottieHandle();
    h->anim = std::move(a);
    h->anim->size(h->width, h->height);
    h->frames = h->anim->totalFrame();
    h->fps = h->anim->frameRate();
    if (h->frames == 0 || h->fps <= 0) {
        delete h;
        return nullptr;
    }
    return h;
}

// pixels: RGBA_8888 (Android Bitmap), premultiplied. stride = bytes per row.
static inline void lottieRender(LottieHandle *h, size_t frame, uint8_t *pixels, size_t w, size_t hgt,
                                size_t stride) {
    if (frame >= h->frames) frame = h->frames - 1;
    for (size_t y = 0; y < hgt; y++) memset(pixels + y * stride, 0, w * 4);
    rlottie::Surface surface(reinterpret_cast<uint32_t *>(pixels), w, hgt, stride);
    h->anim->renderSync(frame, surface);
    // rlottie = ARGB32 premultiplied (memory: B,G,R,A). Android RGBA_8888 = (R,G,B,A) -> R/B swap.
    for (size_t y = 0; y < hgt; y++) {
        uint32_t *row = reinterpret_cast<uint32_t *>(pixels + y * stride);
        for (size_t x = 0; x < w; x++) {
            uint32_t v = row[x];
            row[x] = (v & 0xFF00FF00u) | ((v & 0x00FF0000u) >> 16) | ((v & 0x000000FFu) << 16);
        }
    }
}
