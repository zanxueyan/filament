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

package com.google.android.filament.utils;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.google.android.filament.Engine;

/**
 * IBLPrefilterContext creates and initializes GPU state common to all environment map filters
 * supported. Typically, only one instance per filament Engine of this object needs to exist.
 *
 * Java usage Example:
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * IBLPrefilterContext context(engine);
 * IBLPrefilterContext::HDRLoader hdrLoader(context);
 * IBLPrefilterContext::EquirectangularToCubemap equirectangularToCubemap(context);
 * IBLPrefilterContext::SpecularFilter specularFilter(context);
 *
 * Texture equirect = hdrLoader.run("foo.hdr");
 * Texture skyboxTexture = equirectangularToCubemap.run(equirect);
 * engine.destroy(equirect);
 *
 * Texture reflections = specularFilter.run(skyboxTexture);
 *
 * IndirectLight ibl = IndirectLight::Builder()
 *         .reflections(reflections)
 *         .intensity(30000.0f)
 *         .build(mEngine);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
public class IBLPrefilterContext {
    private long mNativeObject;
    private Engine mEngine;

    public IBLPrefilterContext(Engine engine) {
        mEngine = engine;
        mNativeObject = nCreateIBLPrefilterContext(engine.getNativeObject());
        if (mNativeObject == 0) throw new IllegalStateException("Couldn't create IBLPrefilterContext");
    }

    @Override
    protected void finalize() throws Throwable {
        nDestroy(mNativeObject);
        super.finalize();
    }

    private static native long nCreateIBLPrefilterContext(long nativeEngine);
    private static native void nDestroy(long nativeObject);
}
