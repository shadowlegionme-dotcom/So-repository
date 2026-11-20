#include <jni.h>
#include <android/bitmap.h>
#include "rlottie_core.h"

// Java class: dev.legion.legionClient.RLottie

extern "C" JNIEXPORT jlong JNICALL
Java_dev_legion_legionClient_RLottie_nCreate(JNIEnv *env, jclass, jstring json, jstring key,
                                             jintArray outInfo) {
    if (!json) return 0;
    const char *j = env->GetStringUTFChars(json, nullptr);
    const char *k = key ? env->GetStringUTFChars(key, nullptr) : nullptr;
    // NOTE: rlottie -fno-exceptions flag PUBLIC dey, tai ekhane try/catch lekha jay na
    LottieHandle *h = lottieCreate(j, k);
    env->ReleaseStringUTFChars(json, j);
    if (k) env->ReleaseStringUTFChars(key, k);
    if (!h) return 0;
    if (outInfo && env->GetArrayLength(outInfo) >= 4) {
        jint info[4] = {(jint) h->width, (jint) h->height, (jint) h->frames, (jint) (h->fps * 1000)};
        env->SetIntArrayRegion(outInfo, 0, 4, info);
    }
    return reinterpret_cast<jlong>(h);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_legion_legionClient_RLottie_nDestroy(JNIEnv *, jclass, jlong ptr) {
    delete reinterpret_cast<LottieHandle *>(ptr);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_legion_legionClient_RLottie_nRender(JNIEnv *env, jclass, jlong ptr, jint frame, jobject bitmap) {
    LottieHandle *h = reinterpret_cast<LottieHandle *>(ptr);
    if (!h || !bitmap) return JNI_FALSE;
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) return JNI_FALSE;
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) return JNI_FALSE;
    void *pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) return JNI_FALSE;
    lottieRender(h, (size_t) (frame < 0 ? 0 : frame), (uint8_t *) pixels, info.width, info.height,
                 info.stride);
    AndroidBitmap_unlockPixels(env, bitmap);
    return JNI_TRUE;
}
