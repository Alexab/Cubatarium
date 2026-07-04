package com.cubatarium;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.CRC32;

public final class AssetExtractor {
    private static final String TAG = "Asset";
    private static final String PREFS = "cubatarium_asset_extractor";
    private static final String KEY_VERSION_CODE = "last_extracted_version_code";
    private static final String KEY_ASSET_DIGEST = "last_extracted_digest";

    /** Top-level asset folders copied into files/game/ (TD-006 selective extract). */
    private static final Set<String> GAME_ASSET_WHITELIST = new HashSet<>(Arrays.asList(
            "config.json",
            "config.json.example",
            "content",
            "fonts",
            "models",
            "objects",
            "prefabs",
            "resource_packs",
            "shaders",
            "textures",
            "worlds"
    ));

    private AssetExtractor() {}

    public static void extractIfNeeded(Context context) {
        final int versionCode = getVersionCode(context);
        final SharedPreferences prefs =
                context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        final int stored = prefs.getInt(KEY_VERSION_CODE, -1);
        final String storedDigest = prefs.getString(KEY_ASSET_DIGEST, "");
        final String currentDigest;
        try {
            currentDigest = computeDigest(context.getAssets(), "");
        } catch (IOException e) {
            throw new RuntimeException("Failed to compute asset digest", e);
        }
        final File gameDir = new File(context.getFilesDir(), "game");
        final File flag = new File(context.getFilesDir(), ".assets_extracted");
        final boolean cacheHit =
                stored == versionCode && currentDigest.equals(storedDigest) && flag.exists();
        if (cacheHit && hasCriticalAssets(gameDir)) {
            return;
        }
        if (cacheHit) {
            Log.w(TAG, "Cached assets incomplete, re-extracting into " + gameDir);
        } else {
            Log.i(TAG, "Extracting game assets into " + gameDir);
        }
        try {
            deleteRecursive(gameDir);
            copyAssetFolder(context.getAssets(), "", gameDir);
            if (!flag.exists() && !flag.createNewFile()) {
                flag.createNewFile();
            }
            if (!hasCriticalAssets(gameDir)) {
                throw new IOException("Critical game assets missing after extraction");
            }
            prefs.edit()
                    .putInt(KEY_VERSION_CODE, versionCode)
                    .putString(KEY_ASSET_DIGEST, currentDigest)
                    .apply();
            Log.i(TAG, "Asset extraction complete");
        } catch (IOException e) {
            throw new RuntimeException("Failed to extract game assets", e);
        }
    }

    private static boolean hasCriticalAssets(File gameDir) {
        return new File(gameDir, "fonts/Roboto-Regular.ttf").isFile()
                && new File(gameDir, "shaders/gles/vshader_2d.glsl").isFile()
                && new File(gameDir, "content/types.json").isFile();
    }

    private static void deleteRecursive(File file) {
        if (file == null || !file.exists()) {
            return;
        }
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        if (!file.delete()) {
            Log.w(TAG, "Failed to delete: " + file);
        }
    }

    private static int getVersionCode(Context context) {
        try {
            PackageInfo info = context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0);
            return info.versionCode;
        } catch (PackageManager.NameNotFoundException e) {
            return 0;
        }
    }

    private static boolean shouldExtractRoot(String assetPath) {
        if (assetPath == null || assetPath.isEmpty()) {
            return true;
        }
        final String top = assetPath.contains("/")
                ? assetPath.substring(0, assetPath.indexOf('/'))
                : assetPath;
        return GAME_ASSET_WHITELIST.contains(top);
    }

    private static void copyAssetFolder(AssetManager assets, String assetPath, File destDir)
            throws IOException {
        if (!shouldExtractRoot(assetPath)) {
            return;
        }
        String[] list = assets.list(assetPath);
        if (list == null || list.length == 0) {
            return;
        }
        if (list.length == 1) {
            String only = list[0];
            String childPath = assetPath.isEmpty() ? only : assetPath + "/" + only;
            if (!shouldExtractRoot(childPath)) {
                return;
            }
            String[] nested = assets.list(childPath);
            if (nested != null && nested.length > 0) {
                File childDir = new File(destDir, only);
                if (!childDir.exists() && !childDir.mkdirs()) {
                    throw new IOException("mkdir failed: " + childDir);
                }
                copyAssetFolder(assets, childPath, childDir);
                return;
            }
        }
        if (!destDir.exists() && !destDir.mkdirs()) {
            throw new IOException("mkdir failed: " + destDir);
        }
        for (String name : list) {
            String childPath = assetPath.isEmpty() ? name : assetPath + "/" + name;
            if (!shouldExtractRoot(childPath)) {
                continue;
            }
            String[] nested = assets.list(childPath);
            if (nested != null && nested.length > 0) {
                copyAssetFolder(assets, childPath, new File(destDir, name));
            } else {
                copyAssetFile(assets, childPath, new File(destDir, name));
            }
        }
    }

    private static void copyAssetFile(AssetManager assets, String assetPath, File destFile)
            throws IOException {
        File parent = destFile.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("mkdir failed: " + parent);
        }
        try (InputStream in = assets.open(assetPath);
             OutputStream out = new FileOutputStream(destFile)) {
            byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        }
    }

    private static String computeDigest(AssetManager assets, String assetPath) throws IOException {
        CRC32 crc = new CRC32();
        updateDigest(crc, assets, assetPath);
        return Long.toHexString(crc.getValue());
    }

    private static void updateDigest(CRC32 crc, AssetManager assets, String assetPath)
            throws IOException {
        String[] list = assets.list(assetPath);
        if (list == null || list.length == 0) {
            return;
        }
        for (String name : list) {
            String childPath = assetPath.isEmpty() ? name : assetPath + "/" + name;
            if (!shouldExtractRoot(childPath)) {
                continue;
            }
            String[] nested = assets.list(childPath);
            if (nested != null && nested.length > 0) {
                updateDigest(crc, assets, childPath);
                continue;
            }
            crc.update(childPath.getBytes());
            try (InputStream in = assets.open(childPath)) {
                byte[] buffer = new byte[8192];
                int read;
                while ((read = in.read(buffer)) != -1) {
                    crc.update(buffer, 0, read);
                }
            }
        }
    }
}
