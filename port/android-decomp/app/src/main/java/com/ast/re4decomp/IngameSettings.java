package com.ast.re4decomp;

import android.content.Context;
import android.content.SharedPreferences;
import java.io.File;

/** App-wide options, independent of savegames and EGL context lifetime. */
final class IngameSettings {
  private static SharedPreferences prefs(Context c) {
    return c.getSharedPreferences("ingame-options", Context.MODE_PRIVATE);
  }

  static void restore(Context c) {
    SharedPreferences s = prefs(c);
    NativeBridge.configureGraphics(s.getInt("msaa", 0), s.getInt("af", 8),
        s.getBoolean("mipmapping", true), s.getBoolean("roomFps", true),
        s.getBoolean("gyro", false), s.getBoolean("dof", false));
    NativeBridge.setCheatFlags(s.getInt("cheats", 0) & 3);
    applySoundtrack(c);
  }

  static void graphics(Context c, int msaa, int af, boolean mip, boolean room, boolean gyro, boolean dof) {
    prefs(c).edit().putInt("msaa", msaa).putInt("af", af).putBoolean("mipmapping", mip)
        .putBoolean("roomFps", room).putBoolean("gyro", gyro).putBoolean("dof", dof).apply();
  }

  static void cheats(Context c, int flags) {
    prefs(c).edit().putInt("cheats", flags & 3).apply();
  }

  static File findSoundtrackXwb(Context c) {
    File base = AppStorage.base(c);
    File[] candidates = new File[] {
      new File(base, "mods/audio/bio4bgm.xwb"),
      new File(base, "mods/bio4bgm.xwb"),
      new File(base, "bio4bgm.xwb"),
      new File(base, "game/disc1/files/bgm/bio4bgm.xwb")
    };
    for (File f : candidates) {
      if (f.isFile() && f.length() > 1000000L) {
        return f;
      }
    }
    return null;
  }

  static boolean isRemakeSoundtrackAvailable(Context c) {
    return findSoundtrackXwb(c) != null;
  }

  static int getSoundtrackMode(Context c) {
    SharedPreferences s = prefs(c);
    if (!s.contains("soundtrack_mode") && isRemakeSoundtrackAvailable(c)) {
      return 1;
    }
    return s.getInt("soundtrack_mode", 0);
  }

  static void setSoundtrackMode(Context c, int mode) {
    prefs(c).edit().putInt("soundtrack_mode", mode).apply();
    applySoundtrack(c);
  }

  static void applySoundtrack(Context c) {
    File xwb = findSoundtrackXwb(c);
    int mode = getSoundtrackMode(c);
    boolean enableRemake = (mode == 1) && (xwb != null);
    String path = xwb != null ? xwb.getAbsolutePath() : "";
    CoreBridge.setSoundtrack(enableRemake, path);
  }
}
