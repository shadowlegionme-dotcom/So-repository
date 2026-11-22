#include <jni.h>
#include <android/bitmap.h>
#include <stdio.h>
#include "vpx_core.h"

#define JNI_VISIBLE __attribute__((visibility("default")))

// Java class: dev.legion.legionClient.VpxWebm
// NOTE: Ekta handle ekta thread theke ekshathe use korte hobe (Java side e single render thread).

extern "C" JNI_VISIBLE jlong JNICALL
Java_dev_legion_legionClient_VpxWebm_nOpen(JNIEnv *env, jclass, jstring path, jintArray outInfo) {
    if (!path) return 0;
    const char *p = env->GetStringUTFChars(path, nullptr);
    FILE *f = fopen(p, "rb");
    env->ReleaseStringUTFChars(path, p);
    if (!f) return 0;
    std::vector<uint8_t> buf;
    uint8_t tmp[16384];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) {
        buf.insert(buf.end(), tmp, tmp + n);
        if (buf.size() > (size_t) 32 * 1024 * 1024) break; // sticker boro hobe na
    }
    fclose(f);
    if (buf.empty() || buf.size() > (size_t) 32 * 1024 * 1024) return 0;
    vpxw::Webm *w = vpxw::open(buf.data(), buf.size());
    if (!w) return 0;
    if (outInfo && env->GetArrayLength(outInfo) >= 5) {
        jint info[5] = {w->width, w->height, (jint) w->frames.size(), (jint) w->durationMs,
                        w->alphaMode ? 1 : 0};
        env->SetIntArrayRegion(outInfo, 0, 5, info);
    }
    return reinterpret_cast<jlong>(w);
}

extern "C" JNI_VISIBLE jint JNICALL
Java_dev_legion_legionClient_VpxWebm_nPtsMs(JNIEnv *, jclass, jlong ptr, jint idx) {
    vpxw::Webm *w = reinterpret_cast<vpxw::Webm *>(ptr);
    if (!w || idx < 0 || idx >= (jint) w->frames.size()) return 0;
    return (jint) w->frames[idx].ptsMs;
}

// bitmap == null hole sudhu decode (frame skip), kono pixel likhe na
extern "C" JNI_VISIBLE jboolean JNICALL
Java_dev_legion_legionClient_VpxWebm_nDecode(JNIEnv *env, jclass, jlong ptr, jint idx, jobject bitmap) {
    vpxw::Webm *w = reinterpret_cast<vpxw::Webm *>(ptr);
    if (!w) return JNI_FALSE;
    if (!bitmap) return vpxw::decodeFrame(w, idx, nullptr, 0, 0, 0) ? JNI_TRUE : JNI_FALSE;
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) return JNI_FALSE;
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) return JNI_FALSE;
    void *pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) return JNI_FALSE;
    bool ok = vpxw::decodeFrame(w, idx, (uint8_t *) pixels, (int) info.width, (int) info.height,
                                (size_t) info.stride);
    AndroidBitmap_unlockPixels(env, bitmap);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNI_VISIBLE void JNICALL
Java_dev_legion_legionClient_VpxWebm_nClose(JNIEnv *, jclass, jlong ptr) {
    vpxw::close(reinterpret_cast<vpxw::Webm *>(ptr));
}
