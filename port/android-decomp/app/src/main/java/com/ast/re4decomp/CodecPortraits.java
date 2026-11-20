package com.ast.re4decomp;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Log;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * The portraits of the remake-style radio calls (wish of DevJunior, 20.11.2026: Hunnigan, Salazar, Saddler and Ada
 * in the call's box): from the switched-on mod packs, mods/&lt;pack&gt;/codec/&lt;name&gt;.png (or .jpg, .webp; a later
 * pack wins), scaled to 64 x 64 for the box the app draws (core_mode.cpp codecBuild); codec/calls.txt says which
 * portrait a call shows ("0-13=hunnigan", the log names each call: "codec: call N"). No images in the app itself.
 */
final class CodecPortraits {
  private static final String TAG = "RE4Codec";
  static final int SIZE = 64;

  private CodecPortraits() {}

  private static String read(File f) throws java.io.IOException {
    try (java.io.InputStream in = new java.io.FileInputStream(f)) {
      java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
      byte[] buf = new byte[4096];
      for (int n; (n = in.read(buf)) > 0 && out.size() < 65536;) out.write(buf, 0, n);
      return new String(out.toByteArray(), StandardCharsets.UTF_8);
    }
  }

  static void load(File gameRoot) {
    if (gameRoot == null) return;
    int n = 0;
    StringBuilder callers = new StringBuilder();
    for (String name : ModStore.packs(gameRoot)) {
      File pack = new File(ModStore.dir(gameRoot), name);
      if (!ModSwitch.isOn(pack)) continue;
      File[] files = new File(pack, "codec").listFiles();
      if (files == null) continue;
      java.util.Arrays.sort(files);
      for (File f : files) {
        String low = f.getName().toLowerCase(Locale.ROOT);
        if (low.equals("calls.txt")) {
          try {
            callers.append(read(f)).append('\n');
          } catch (Exception e) {
            Log.w(TAG, f + ": " + e);
          }
          continue;
        }
        int dot = low.lastIndexOf('.');
        if (dot <= 0 || !(low.endsWith(".png") || low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".webp"))) {
          continue;
        }
        Bitmap b = BitmapFactory.decodeFile(f.getAbsolutePath());
        if (b == null) {
          Log.w(TAG, f + ": not an image");
          continue;
        }
        Bitmap s = Bitmap.createScaledBitmap(b, SIZE, SIZE, true);
        int[] px = new int[SIZE * SIZE];
        s.getPixels(px, 0, SIZE, 0, 0, SIZE, SIZE);
        CoreBridge.setCodecPortrait(low.substring(0, dot), px);
        n++;
      }
    }
    CoreBridge.setCodecCallers(callers.toString());
    if (n > 0) Log.i(TAG, n + " radio portraits from the mods");
  }
}
