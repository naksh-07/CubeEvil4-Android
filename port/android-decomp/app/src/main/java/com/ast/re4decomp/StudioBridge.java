package com.ast.re4decomp;

/**
 * CubeEvil4 Studio's native viewer (cpp/studio_jni.cpp, the library libre4studio: the Studio's own APK loads only it,
 * without the game core): archives, models, animations, the GL picture.
 */
final class StudioBridge {
  private StudioBridge() {}

  private static boolean loaded;

  static synchronized void load() {
    if (!loaded) {
      System.loadLibrary("re4studio");
      loaded = true;
    }
  }

  /** "" or why not; `name` the file's own name (the path may be /proc/self/fd/N). */
  static native String openArchive(String path, String name);
  /** "index\tkind\tbytes" per entry of the archive opened. */
  static native String[] entries();
  static native String loadModel(int bin, int tpl);
  /** The whole model as the game puts it together (or the archive's first model): "" or why not. */
  static native String loadComplete();
  /** The pieces on the view, the main model first: "entry\tname". */
  static native String[] parts();
  /** The names the game gives the archive's models: "entry\tname" (none for an archive it does not know). */
  static native String[] partNames();
  /** The piece drawn tinted (its place in parts()), -1 none. */
  static native void highlight(int piece);
  static native String togglePiece(int bin);
  static native int useMotions(String path, String name);
  static native String lastError();
  static native String[] motionNames();
  static native String setMotion(int clip);
  static native float motionLength();
  static native void setInPlace(boolean on);
  static native float[] frameModel();
  static native String info();
  static native void surfaceCreated();
  static native void draw(int width, int height, float yaw, float pitch, float distance, float tx, float ty, float tz,
      int flags, float frame);
}
