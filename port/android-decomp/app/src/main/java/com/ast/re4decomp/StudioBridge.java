package com.ast.re4decomp;

/** CubeEvil4 Studio's native viewer (cpp/studio_jni.cpp): archives, models, animations, the GL picture. */
final class StudioBridge {
  private StudioBridge() {}

  static void load() { CoreBridge.load(); }

  static native String openArchive(String path);
  /** "index\tkind\tbytes" per entry of the archive opened. */
  static native String[] entries();
  static native String loadModel(int bin, int tpl);
  static native String togglePiece(int bin);
  static native int useMotions(String path);
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
