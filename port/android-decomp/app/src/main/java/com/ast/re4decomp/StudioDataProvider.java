package com.ast.re4decomp;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Binder;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.Arrays;
import java.util.List;

/**
 * The game's data for CubeEvil4 Studio (Mods, 19.11.2026; DevJunior: "the Studio as an APK of its own", the flavour
 * "studio"): the imported discs and the mod packs (the folder game/ of AppStorage), read only, for an app signed with
 * the same key and for nobody else (a caller of another signature gets nothing; no permission to declare, so the order
 * in which the two apps are installed does not matter). Android keeps another app's folder under Android/data closed
 * to the Studio; this hands it the files instead.
 *
 * content://AUTHORITY/list/REL  a folder: rows name, dir (0/1), size
 * content://AUTHORITY/file/REL  a file, opened read only
 * REL is a path below game/ ("disc1/files/em/pl00.drs"); "..", hidden names and anything outside are refused.
 */
public final class StudioDataProvider extends ContentProvider {
  static final String[] COLUMNS = {"name", "dir", "size"};

  @Override public boolean onCreate() { return true; }

  private void allow() throws SecurityException {
    int caller = Binder.getCallingUid();
    if (caller == Process.myUid()) return;
    PackageManager pm = getContext().getPackageManager();
    int result = pm.checkSignatures(Process.myUid(), caller);
    if (result != PackageManager.SIGNATURE_MATCH) {
      android.util.Log.w("RE4Studio", "data provider: caller uid " + caller + " refused (signature check " + result + ")");
      throw new SecurityException("CubeEvil4 Studio data: only for an app signed like this one (check " + result + ")");
    }
  }

  /** The file below game/ that `uri` names (after "list" or "file"), or null. */
  private File resolve(Uri uri, String kind) {
    List<String> segments = uri.getPathSegments();
    if (segments.isEmpty() || !segments.get(0).equals(kind)) return null;
    File root = new File(AppStorage.base(getContext()), "game");
    File f = root;
    for (String s : segments.subList(1, segments.size())) {
      if (s.isEmpty() || s.equals("..") || s.equals(".") || s.startsWith(".") || s.contains("/")) return null;
      f = new File(f, s);
    }
    try {
      String r = root.getCanonicalPath(), p = f.getCanonicalPath();
      if (!p.equals(r) && !p.startsWith(r + File.separator)) return null;
    } catch (IOException e) {
      return null;
    }
    return f;
  }

  @Override public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
    allow();
    File dir = resolve(uri, "list");
    MatrixCursor out = new MatrixCursor(COLUMNS);
    File[] files = dir != null ? dir.listFiles() : null;
    if (files == null) return out;
    Arrays.sort(files);
    for (File f : files) {
      if (f.getName().startsWith(".")) continue;
      out.addRow(new Object[] {f.getName(), f.isDirectory() ? 1 : 0, f.isFile() ? f.length() : 0L});
    }
    return out;
  }

  @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
    allow();
    if (mode != null && !mode.equals("r")) throw new FileNotFoundException("read only");
    File f = resolve(uri, "file");
    if (f == null || !f.isFile()) throw new FileNotFoundException(String.valueOf(uri));
    return ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY);
  }

  @Override public String getType(Uri uri) { return "application/octet-stream"; }

  @Override public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }

  @Override public int delete(Uri uri, String selection, String[] args) { throw new UnsupportedOperationException(); }

  @Override public int update(Uri uri, ContentValues values, String selection, String[] args) {
    throw new UnsupportedOperationException();
  }
}
