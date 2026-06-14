package com.cubatarium;

import android.os.Bundle;

import com.google.androidgamesdk.GameActivity;

public class MainActivity extends GameActivity {
    static {
        System.loadLibrary("cubatarium");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            AssetExtractor.extractIfNeeded(this);
        } catch (RuntimeException e) {
            android.util.Log.e("Cubatarium", "Asset extraction failed", e);
            throw e;
        }
        nativeOnCreate();
    }

    private native void nativeOnCreate();
}
