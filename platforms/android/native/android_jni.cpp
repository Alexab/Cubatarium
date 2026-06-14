#include "android_jni.h"

#include <android/asset_manager_jni.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>
#include <atomic>

namespace
{

JavaVM *gJvm = nullptr;
jobject gActivity = nullptr;
AAssetManager *gAssetManager = nullptr;
std::string gFilesDir;
std::atomic<bool> gJavaInitDone{false};

std::string JStringToStd(JNIEnv *env, jstring value)
{
  if (!value)
  {
    return {};
  }
  const char *utf = env->GetStringUTFChars(value, nullptr);
  std::string out = utf ? utf : "";
  if (utf)
  {
    env->ReleaseStringUTFChars(value, utf);
  }
  return out;
}

} // namespace

AAssetManager *CubatariumAndroidGetAssetManager() { return gAssetManager; }

std::string CubatariumAndroidGetFilesDir() { return gFilesDir; }

bool CubatariumAndroidWaitForJavaInit(android_app *app)
{
  while (!gJavaInitDone.load(std::memory_order_acquire) &&
         app->destroyRequested == 0)
  {
    int events = 0;
    android_poll_source *source = nullptr;
    while (ALooper_pollOnce(0, nullptr, &events,
                            reinterpret_cast<void **>(&source)) >= 0)
    {
      if (source)
      {
        source->process(app, source);
      }
      if (app->destroyRequested != 0)
      {
        return false;
      }
    }
  }
  return gJavaInitDone.load(std::memory_order_acquire);
}

extern "C" JNIEXPORT void JNICALL
Java_com_cubatarium_MainActivity_nativeOnCreate(JNIEnv *env, jobject activity)
{
  env->GetJavaVM(&gJvm);
  if (gActivity)
  {
    env->DeleteGlobalRef(gActivity);
  }
  gActivity = env->NewGlobalRef(activity);

  jclass activityClass = env->GetObjectClass(activity);
  jmethodID getFilesDir =
      env->GetMethodID(activityClass, "getFilesDir", "()Ljava/io/File;");
  jobject filesDirObj = env->CallObjectMethod(activity, getFilesDir);
  jclass fileClass = env->FindClass("java/io/File");
  jmethodID getAbsolutePath =
      env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
  gFilesDir =
      JStringToStd(env, static_cast<jstring>(env->CallObjectMethod(
                            filesDirObj, getAbsolutePath)));

  jmethodID getAssetManager =
      env->GetMethodID(activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
  jobject assetManagerObj = env->CallObjectMethod(activity, getAssetManager);
  gAssetManager = AAssetManager_fromJava(env, assetManagerObj);
  gJavaInitDone.store(true, std::memory_order_release);
}
