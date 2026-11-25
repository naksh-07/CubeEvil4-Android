package com.ast.re4decomp;

import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Rect;
import android.util.Log;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;

/**
 * Loads, slices, and caches authentic Capcom RE4 Remake (iOS) texture atlases.
 * Decodes the 1024x512 virtualpad buttons atlas and 512x512 button prompts atlas
 * into hardware-ready bitmaps for Re4rVirtualPadView.
 */
public final class Re4rTouchAtlas {
  private static final String TAG = "Re4rTouchAtlas";
  private static final String ASSET_DIR = "touch_re4r/";
  private static final String ATLAS_BUTTONS = ASSET_DIR + "re4r_virtualpad_buttons_atlas.png";
  private static final String ATLAS_PROMPTS = ASSET_DIR + "re4r_button_prompts_atlas.png";
  private static final String ATLAS_GLYPHS = ASSET_DIR + "re4r_all_gamepad_keyboard_glyphs.png";

  // VirtualPad Slices (from re4r_virtualpad_buttons_atlas.png, 1024x512)
  private Bitmap dpadBase;
  private Bitmap btnNormal;
  private Bitmap btnPressed;
  private Bitmap capsuleNormal;
  private Bitmap capsulePressed;
  private Bitmap stickRing;
  private Bitmap stickKnob;

  // Prompts Slices (from re4r_button_prompts_atlas.png, 512x512)
  private final Map<String, Bitmap> glyphs = new HashMap<>();

  private boolean loaded;

  public Re4rTouchAtlas(Context context) {
    load(context);
  }

  private void load(Context context) {
    if (loaded) return;
    try {
      AssetManager am = context.getAssets();
      BitmapFactory.Options opts = new BitmapFactory.Options();
      opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
      opts.inScaled = false;

      // 1. Load VirtualPad Buttons Atlas
      InputStream isButtons = am.open(ATLAS_BUTTONS);
      Bitmap buttonsAtlas = BitmapFactory.decodeStream(isButtons, null, opts);
      if (isButtons != null) isButtons.close();

      if (buttonsAtlas != null) {
        // D-Pad Cross Base (268x268 at 10, 10)
        dpadBase = slice(buttonsAtlas, 10, 10, 268, 268);
        // Squircle Button Normal Dark Glass (208x208 at 300, 12)
        btnNormal = slice(buttonsAtlas, 300, 12, 208, 208);
        // Squircle Button Pressed Golden Amber (220x220 at 526, 6)
        btnPressed = slice(buttonsAtlas, 526, 6, 220, 220);
        // Capsule Pill Button Normal Dark Glass (166x122 at 205, 309)
        capsuleNormal = slice(buttonsAtlas, 205, 309, 166, 122);
        // Capsule Pill Button Pressed Golden Amber (178x134 at 7, 303)
        capsulePressed = slice(buttonsAtlas, 7, 303, 178, 134);
        // Stick Outer Ring Arc (255x255 at 758, 2)
        stickRing = slice(buttonsAtlas, 758, 2, 255, 255);
        // Stick Center Knob Cap (139x139 at 738, 266)
        stickKnob = slice(buttonsAtlas, 738, 266, 139, 139);
        buttonsAtlas.recycle();
      } else {
        Log.e(TAG, "Failed to decode buttons atlas from " + ATLAS_BUTTONS);
      }

      // 2. Load Button Prompts Atlas (512x512)
      InputStream isPrompts = am.open(ATLAS_PROMPTS);
      Bitmap promptsAtlas = BitmapFactory.decodeStream(isPrompts, null, opts);
      if (isPrompts != null) isPrompts.close();

      if (promptsAtlas != null) {
        glyphs.put(CoreTouch.A, slice(promptsAtlas, 30, 23, 51, 64));
        glyphs.put(CoreTouch.B, slice(promptsAtlas, 144, 21, 48, 70));
        glyphs.put(CoreTouch.X, slice(promptsAtlas, 257, 24, 46, 64));
        glyphs.put(CoreTouch.Y, slice(promptsAtlas, 369, 24, 46, 63));
        glyphs.put(CoreTouch.MENU, slice(promptsAtlas, 12, 135, 89, 66));
        glyphs.put(CoreTouch.START, slice(promptsAtlas, 132, 140, 72, 56));
        glyphs.put(CoreTouch.L, slice(promptsAtlas, 17, 363, 95, 96));  // LT / Knife
        glyphs.put(CoreTouch.R, slice(promptsAtlas, 143, 363, 107, 93)); // RT / Aim
        glyphs.put(CoreTouch.Z, slice(promptsAtlas, 391, 362, 93, 79));  // RB / Map
        glyphs.put("LB", slice(promptsAtlas, 280, 362, 84, 79));
        glyphs.put("RB", slice(promptsAtlas, 391, 362, 93, 79));
        promptsAtlas.recycle();
      } else {
        Log.e(TAG, "Failed to decode prompts atlas from " + ATLAS_PROMPTS);
      }

      loaded = true;
      Log.i(TAG, "Loaded authentic RE4R touch atlases successfully");
    } catch (Throwable e) {
      Log.e(TAG, "Error loading RE4R touch atlases", e);
    }
  }

  private static Bitmap slice(Bitmap source, int x, int y, int width, int height) {
    if (source == null) return null;
    int safeX = Math.max(0, Math.min(x, source.getWidth() - 1));
    int safeY = Math.max(0, Math.min(y, source.getHeight() - 1));
    int safeW = Math.min(width, source.getWidth() - safeX);
    int safeH = Math.min(height, source.getHeight() - safeY);
    if (safeW <= 0 || safeH <= 0) return null;
    return Bitmap.createBitmap(source, safeX, safeY, safeW, safeH);
  }

  public Bitmap getDpadBase() { return dpadBase; }
  public Bitmap getButtonNormal() { return btnNormal; }
  public Bitmap getButtonPressed() { return btnPressed; }
  public Bitmap getCapsuleNormal() { return capsuleNormal; }
  public Bitmap getCapsulePressed() { return capsulePressed; }
  public Bitmap getStickRing() { return stickRing; }
  public Bitmap getStickKnob() { return stickKnob; }

  public Bitmap getGlyph(String action) {
    return glyphs.get(action);
  }

  public boolean isLoaded() {
    return loaded && btnNormal != null && btnPressed != null;
  }

  public void destroy() {
    recycle(dpadBase);
    recycle(btnNormal);
    recycle(btnPressed);
    recycle(capsuleNormal);
    recycle(capsulePressed);
    recycle(stickRing);
    recycle(stickKnob);
    for (Bitmap b : glyphs.values()) recycle(b);
    glyphs.clear();
    loaded = false;
  }

  private static void recycle(Bitmap b) {
    if (b != null && !b.isRecycled()) b.recycle();
  }
}
