/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <jni.h>

#include <filament-iblprefilter/IBLPrefilterContext.h>

#include <filament/Engine.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR

#include <stb_image.h>

using namespace filament;

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_utils_IBLPrefilterContext_nCreateIBLPrefilterContext(JNIEnv* env, jclass,
        jlong nativeEngine) {
    Engine* engine = (Engine*) nativeEngine;
    return (jlong) new IBLPrefilterContext(*engine);
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_android_filament_utils_IBLPrefilterContext_nDestroy(JNIEnv*, jclass, jlong native) {
    IBLPrefilterContext* context = (IBLPrefilterContext*) native;
    delete context;
}
