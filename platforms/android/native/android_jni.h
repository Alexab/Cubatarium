#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#include <android/asset_manager.h>
#include <string>

AAssetManager *CubatariumAndroidGetAssetManager();
std::string CubatariumAndroidGetFilesDir();
/// Blocks until MainActivity.nativeOnCreate() has run (or destroy requested).
bool CubatariumAndroidWaitForJavaInit(struct android_app *app);
void CubatariumAndroidFinishActivity();

#endif
