package com.ast.re4decomp;

import android.content.ContentResolver;
import android.content.Context;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Where CubeEvil4 Studio reads the discs and mod packs (paths below the game's folder game/, "disc1/files/em/pl00.drs"):
 * the app's own folder (the Studio inside a game build), or, in the Studio's own APK (Mods, 19.11.2026), the game app's
 * through StudioDataProvider. The native viewer reads paths: a file of the game app is handed over open, as
 * /proc/self/fd/N, kept open until another one replaces it or close().
 */
final class StudioFiles {
  /** One name of a folder. */
  static final class Item {
    final String name;
    final boolean dir;
    final long size;

    Item(String name, boolean dir, long size) {
      this.name = name;
      this.dir = dir;
      this.size = size;
    }
  }

  // the game apps the Studio APK reads, in this order: the mods' test build, the official app (once it has the
  // provider)
  private static final String[] GAME_APPS = {"com.ast.cubeevil4.mods", "com.ast.cubeevil4"};

  private final File local;          // the app's own game/ folder, or null
  private final String authority;    // the game app's provider, or null
  private final String app;          // its package (for the message)
  private final ContentResolver resolver;
  private final Map<String, ParcelFileDescriptor> open = new HashMap<>();

  private StudioFiles(File local, String authority, String app, ContentResolver resolver) {
    this.local = local;
    this.authority = authority;
    this.app = app;
    this.resolver = resolver;
  }

  /** The Studio APK: the game app's files (null when none is installed); a game build: its own folder. */
  static StudioFiles open(Context c) {
    if (!BuildConfig.STUDIO_APP) return new StudioFiles(new File(AppStorage.base(c), "game"), null, null, null);
    PackageManager pm = c.getPackageManager();
    for (String app : GAME_APPS) {
      String authority = app + ".studiodata";
      if (pm.resolveContentProvider(authority, 0) != null) {
        return new StudioFiles(null, authority, app, c.getContentResolver());
      }
    }
    return null;
  }

  /** Which app the files come from ("" the Studio's own). */
  String from() { return app != null ? app : ""; }

  List<Item> list(String rel) {
    List<Item> out = new ArrayList<>();
    if (local != null) {
      File[] files = new File(local, rel).listFiles();
      if (files == null) return out;
      Arrays.sort(files);
      for (File f : files) out.add(new Item(f.getName(), f.isDirectory(), f.isFile() ? f.length() : 0));
      return out;
    }
    try (Cursor cur = resolver.query(uri("list", rel), null, null, null, null)) {
      if (cur == null) return out;
      while (cur.moveToNext()) out.add(new Item(cur.getString(0), cur.getInt(1) != 0, cur.getLong(2)));
    } catch (RuntimeException e) {
      android.util.Log.w("RE4Studio", "list " + rel + ": " + e);
    }
    return out;
  }

  /** Whether `rel` is a file (and how big: -1 none). */
  long size(String rel) {
    if (local != null) {
      File f = new File(local, rel);
      return f.isFile() ? f.length() : -1;
    }
    int slash = rel.lastIndexOf('/');
    String dir = slash < 0 ? "" : rel.substring(0, slash), name = rel.substring(slash + 1);
    for (Item i : list(dir)) if (!i.dir && i.name.equals(name)) return i.size;
    return -1;
  }

  /** A path the native viewer can read for `rel`, or null. */
  String path(String rel) {
    if (local != null) {
      File f = new File(local, rel);
      return f.isFile() ? f.getAbsolutePath() : null;
    }
    ParcelFileDescriptor kept = open.get(rel);
    if (kept != null) return "/proc/self/fd/" + kept.getFd();
    try {
      ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri("file", rel), "r");
      if (pfd == null) return null;
      // a few at a time: the model's archive and an animation source
      if (open.size() >= 6) close();
      open.put(rel, pfd);
      return "/proc/self/fd/" + pfd.getFd();
    } catch (Exception e) {
      android.util.Log.w("RE4Studio", "open " + rel + ": " + e);
      return null;
    }
  }

  void close() {
    for (ParcelFileDescriptor p : open.values()) {
      try {
        p.close();
      } catch (Exception ignored) {
      }
    }
    open.clear();
  }

  private Uri uri(String kind, String rel) {
    Uri.Builder b = new Uri.Builder().scheme("content").authority(authority).appendPath(kind);
    for (String s : rel.split("/")) if (!s.isEmpty()) b.appendPath(s);
    return b.build();
  }
}
