package com.cubatarium;

import android.content.Context;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public final class AssetExtractor {
    private AssetExtractor() {}

    public static void extractIfNeeded(Context context) {
        File flag = new File(context.getFilesDir(), ".assets_extracted");
        if (flag.exists()) {
            return;
        }
        File gameDir = new File(context.getFilesDir(), "game");
        try {
            copyAssetFolder(context.getAssets(), "", gameDir);
            if (!flag.createNewFile()) {
                flag.createNewFile();
            }
        } catch (IOException e) {
            throw new RuntimeException("Failed to extract game assets", e);
        }
    }

    private static void copyAssetFolder(AssetManager assets, String assetPath, File destDir)
            throws IOException {
        String[] list = assets.list(assetPath);
        if (list == null || list.length == 0) {
            return;
        }
        if (list.length == 1) {
            String only = list[0];
            String childPath = assetPath.isEmpty() ? only : assetPath + "/" + only;
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
}
