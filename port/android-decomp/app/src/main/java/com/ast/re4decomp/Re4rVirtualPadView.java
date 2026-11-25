package com.ast.re4decomp;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.util.AttributeSet;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Authentic Capcom Resident Evil 4 Remake (iOS) Touch HUD overlay.
 * Blits real decoded PNG texture atlases from Re4rTouchAtlas using hardware-accelerated
 * Canvas drawing, bypassing RetroTouch vector limitations completely.
 *
 * Implements:
 * - Real textured D-pad cross and analog thumbstick with movable knob.
 * - Dual-state texture swapping (obsidian dark glass -> illuminated golden-amber glow).
 * - Multi-touch interaction feeding directly into CoreTouch and host.send().
 * - 3.0s inactivity fading (VirtualPad_op_08_3Sec) down to 25% opacity.
 * - Right-half look zone for camera/C-Stick swipe gestures.
 */
public final class Re4rVirtualPadView extends View {
  private static final String TAG = "Re4rVirtualPadView";

  // Inactivity fading: 3.0s delay, fade to 45% alpha
  private static final long FADE_DELAY_MS = 3000L;
  private static final float MIN_ALPHA = 0.45f;
  private static final float ACTIVE_ALPHA = 1.0f;

  private final Re4rTouchAtlas atlas;
  private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
  private final Paint glyphPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);

  private CoreTouch coreTouch;
  private TouchControls.Host host;

  // Screen metrics & Cutout safe insets
  private int viewWidth, viewHeight;
  private int safeLeft, safeTop, safeRight, safeBottom;

  // Alpha animation
  private float currentAlpha = ACTIVE_ALPHA;
  private ValueAnimator fadeAnimator;
  private final Handler fadeHandler = new Handler(Looper.getMainLooper());
  private final Runnable fadeRunnable = this::startFadeOut;

  // --- Left Analog Thumbstick ---
  private float stickCenterX, stickCenterY, stickRadius, maxDisplacement;
  private float knobX, knobY, knobRadius;
  private int stickPointerId = -1;
  private final RectF stickRingRect = new RectF();
  private final RectF knobRect = new RectF();

  // --- Left D-Pad Cross ---
  private float dpadCenterX, dpadCenterY, dpadSize;
  private int dpadPointerId = -1;
  private final RectF dpadRect = new RectF();
  private boolean dpadUp, dpadDown, dpadLeft, dpadRight;

  // --- Buttons ---
  public static final class PadButton {
    final String action;
    final boolean isCapsule;
    final RectF bounds = new RectF();
    boolean pressed;
    boolean latched;
    int pointerId = -1;

    PadButton(String action, boolean isCapsule) {
      this.action = action;
      this.isCapsule = isCapsule;
    }
  }

  private final List<PadButton> buttons = new ArrayList<>();
  private final Map<String, PadButton> buttonMap = new HashMap<>();

  // --- Right Screen Camera Look / Swipe ---
  private int lookPointerId = -1;
  private float prevLookX, prevLookY;

  public Re4rVirtualPadView(Context context) {
    this(context, null);
  }

  public Re4rVirtualPadView(Context context, AttributeSet attrs) {
    super(context, attrs);
    atlas = new Re4rTouchAtlas(context);
    setFocusable(false);
    initButtons();
  }

  public void init(CoreTouch coreTouch, TouchControls.Host host) {
    this.coreTouch = coreTouch;
    this.host = host;
  }

  private void initButtons() {
    buttons.clear();
    buttonMap.clear();

    // Face Buttons (Squircle)
    addButton(CoreTouch.A, false);
    addButton(CoreTouch.B, false);
    addButton(CoreTouch.X, false);
    addButton(CoreTouch.Y, false);

    // Shoulders & Utility (Capsule Pill)
    addButton(CoreTouch.L, true);
    addButton(CoreTouch.R, true);
    addButton(CoreTouch.Z, true);
    addButton(CoreTouch.MENU, true);
    addButton(CoreTouch.START, true);
  }

  private void addButton(String action, boolean isCapsule) {
    PadButton btn = new PadButton(action, isCapsule);
    buttons.add(btn);
    buttonMap.put(action, btn);
  }

  public void setSafeInsets(int left, int top, int right, int bottom) {
    if (safeLeft == left && safeTop == top && safeRight == right && safeBottom == bottom) return;
    safeLeft = left;
    safeTop = top;
    safeRight = right;
    safeBottom = bottom;
    recalculateLayout();
    invalidate();
  }

  @Override
  protected void onSizeChanged(int w, int h, int oldw, int oldh) {
    super.onSizeChanged(w, h, oldw, oldh);
    viewWidth = w;
    viewHeight = h;
    recalculateLayout();
    resetFadeTimer();
  }

  private void recalculateLayout() {
    if (viewWidth <= 0 || viewHeight <= 0) {
      viewWidth = getWidth();
      viewHeight = getHeight();
    }
    if (viewWidth <= 0 || viewHeight <= 0) return;

    float w = viewWidth;
    float h = viewHeight;
    float effLeft = safeLeft > 0 ? safeLeft : w * 0.03f;
    float effRight = w - (safeRight > 0 ? safeRight : w * 0.03f);
    float effWidth = effRight - effLeft;

    // 1. Left Analog Stick (bottom left)
    stickRadius = h * 0.15f; // diameter ~ 30% screen height
    maxDisplacement = stickRadius * 0.85f;
    knobRadius = stickRadius * 0.52f;
    stickCenterX = effLeft + effWidth * 0.12f;
    stickCenterY = h * 0.72f;
    stickRingRect.set(stickCenterX - stickRadius, stickCenterY - stickRadius,
        stickCenterX + stickRadius, stickCenterY + stickRadius);
    if (stickPointerId == -1) {
      knobX = stickCenterX;
      knobY = stickCenterY;
    }
    updateKnobRect();

    // 2. Left D-Pad Cross (above stick)
    dpadSize = h * 0.24f;
    dpadCenterX = effLeft + effWidth * 0.11f;
    dpadCenterY = h * 0.36f;
    float halfDpad = dpadSize * 0.5f;
    dpadRect.set(dpadCenterX - halfDpad, dpadCenterY - halfDpad,
        dpadCenterX + halfDpad, dpadCenterY + halfDpad);

    // 3. Top Shoulder Triggers (Capsule Pill)
    float capsuleW = effWidth * 0.12f;
    float capsuleH = capsuleW * 0.62f;

    // L (Knife Trigger) - Top Left
    setButtonBounds(CoreTouch.L, effLeft + effWidth * 0.12f, h * 0.12f, capsuleW, capsuleH);

    // R (Aim Weapon Trigger) - Top Right
    setButtonBounds(CoreTouch.R, effRight - effWidth * 0.12f, h * 0.12f, capsuleW, capsuleH);

    // Z (Map / Secondary) - Top Right-Inner
    setButtonBounds(CoreTouch.Z, effRight - effWidth * 0.26f, h * 0.12f, capsuleW * 0.88f, capsuleH);

    // 4. Center Utility Buttons (View / Menu)
    float utilW = effWidth * 0.085f;
    float utilH = utilW * 0.60f;
    setButtonBounds(CoreTouch.MENU, effLeft + effWidth * 0.44f, h * 0.08f, utilW, utilH);
    setButtonBounds(CoreTouch.START, effLeft + effWidth * 0.56f, h * 0.08f, utilW, utilH);

    // 5. Right Action Squircle Cluster (Ergonomic Arc)
    float btnSizeA = h * 0.18f; // Large primary A button
    float btnSizeOther = h * 0.135f;

    // A (Fire / Kick)
    setButtonBounds(CoreTouch.A, effRight - effWidth * 0.12f, h * 0.67f, btnSizeA, btnSizeA);
    // B (Cancel / Sprint)
    setButtonBounds(CoreTouch.B, effRight - effWidth * 0.23f, h * 0.84f, btnSizeOther, btnSizeOther);
    // X (Ashley Command)
    setButtonBounds(CoreTouch.X, effRight - effWidth * 0.04f, h * 0.50f, btnSizeOther, btnSizeOther);
    // Y (Attache Case)
    setButtonBounds(CoreTouch.Y, effRight - effWidth * 0.17f, h * 0.43f, btnSizeOther, btnSizeOther);
  }

  private void setButtonBounds(String action, float cx, float cy, float w, float h) {
    PadButton btn = buttonMap.get(action);
    if (btn != null) {
      btn.bounds.set(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
    }
  }

  private void updateKnobRect() {
    knobRect.set(knobX - knobRadius, knobY - knobRadius, knobX + knobRadius, knobY + knobRadius);
  }

  // --- Activity / Touch Control Collision Test ---
  public boolean onControl(float x, float y) {
    if (getVisibility() != VISIBLE) return false;
    // Check Stick
    if (Math.hypot(x - stickCenterX, y - stickCenterY) <= stickRadius * 1.2f) return true;
    // Check D-Pad
    if (dpadRect.contains(x, y)) return true;
    // Check Buttons
    for (PadButton b : buttons) {
      if (b.bounds.contains(x, y)) return true;
    }
    return false;
  }

  // --- Latch State Tracking ---
  public void setLatched(String action, boolean latched) {
    PadButton btn = buttonMap.get(action);
    if (btn != null) {
      btn.latched = latched;
      invalidate();
    }
  }

  // --- Inactivity Fading Logic ---
  private void resetFadeTimer() {
    fadeHandler.removeCallbacks(fadeRunnable);
    if (fadeAnimator != null && fadeAnimator.isRunning()) {
      fadeAnimator.cancel();
    }
    if (currentAlpha != ACTIVE_ALPHA) {
      currentAlpha = ACTIVE_ALPHA;
      invalidate();
    }
    fadeHandler.postDelayed(fadeRunnable, FADE_DELAY_MS);
  }

  private void startFadeOut() {
    if (fadeAnimator != null) fadeAnimator.cancel();
    fadeAnimator = ValueAnimator.ofFloat(currentAlpha, MIN_ALPHA);
    fadeAnimator.setDuration(400);
    fadeAnimator.addUpdateListener(anim -> {
      currentAlpha = (float) anim.getAnimatedValue();
      invalidate();
    });
    fadeAnimator.start();
  }

  public void resetInputState() {
    stickPointerId = -1;
    knobX = stickCenterX;
    knobY = stickCenterY;
    updateKnobRect();

    dpadPointerId = -1;
    releaseDpad();

    for (PadButton b : buttons) {
      b.pressed = false;
      b.pointerId = -1;
    }

    lookPointerId = -1;
    invalidate();
  }

  // --- Touch Event Handling ---
  @Override
  public boolean onTouchEvent(MotionEvent e) {
    if (getVisibility() != VISIBLE) return false;
    resetFadeTimer();

    int action = e.getActionMasked();
    int actionIndex = e.getActionIndex();
    int pointerId = e.getPointerId(actionIndex);
    float x = e.getX(actionIndex);
    float y = e.getY(actionIndex);

    switch (action) {
      case MotionEvent.ACTION_DOWN:
      case MotionEvent.ACTION_POINTER_DOWN:
        handlePointerDown(pointerId, x, y);
        break;

      case MotionEvent.ACTION_MOVE:
        handlePointerMove(e);
        break;

      case MotionEvent.ACTION_POINTER_UP:
      case MotionEvent.ACTION_UP:
        handlePointerUp(pointerId);
        break;

      case MotionEvent.ACTION_CANCEL:
        resetInputState();
        if (coreTouch != null) {
          coreTouch.cancel();
          if (host != null) host.send();
        }
        break;
    }

    invalidate();
    return true;
  }

  private void handlePointerDown(int id, float x, float y) {
    // 1. Check Stick
    if (stickPointerId == -1 && Math.hypot(x - stickCenterX, y - stickCenterY) <= stickRadius * 1.25f) {
      stickPointerId = id;
      updateStickPosition(x, y);
      return;
    }

    // 2. Check D-Pad
    if (dpadPointerId == -1 && dpadRect.contains(x, y)) {
      dpadPointerId = id;
      updateDpadDirection(x, y);
      return;
    }

    // 3. Check Buttons
    for (PadButton b : buttons) {
      if (b.pointerId == -1 && b.bounds.contains(x, y)) {
        b.pointerId = id;
        b.pressed = true;
        if (coreTouch != null) {
          coreTouch.action(b.action, true);
          if (host != null) host.send();
        }
        return;
      }
    }

    // 4. Right-Half Look Zone (C-Stick camera swipe)
    if (lookPointerId == -1 && x > viewWidth * 0.45f) {
      lookPointerId = id;
      prevLookX = x;
      prevLookY = y;
      if (coreTouch != null) {
        coreTouch.fingerDown(id, true);
      }
    }
  }

  private void handlePointerMove(MotionEvent e) {
    int count = e.getPointerCount();
    for (int i = 0; i < count; i++) {
      int id = e.getPointerId(i);
      float x = e.getX(i);
      float y = e.getY(i);

      if (id == stickPointerId) {
        updateStickPosition(x, y);
      } else if (id == dpadPointerId) {
        updateDpadDirection(x, y);
      } else if (id == lookPointerId) {
        float dx = (x - prevLookX) / Math.min(viewWidth, viewHeight);
        float dy = (y - prevLookY) / Math.min(viewWidth, viewHeight);
        prevLookX = x;
        prevLookY = y;
        if (coreTouch != null) {
          coreTouch.look(dx, dy);
          if (host != null) host.send();
        }
      } else {
        // Track button sliding or dragging
        for (PadButton b : buttons) {
          if (b.pointerId == id) {
            boolean inside = b.bounds.contains(x, y);
            if (inside != b.pressed) {
              b.pressed = inside;
              if (coreTouch != null) {
                coreTouch.action(b.action, inside);
                if (host != null) host.send();
              }
            }
          }
        }
      }
    }
  }

  private void handlePointerUp(int id) {
    if (id == stickPointerId) {
      stickPointerId = -1;
      knobX = stickCenterX;
      knobY = stickCenterY;
      updateKnobRect();
      if (coreTouch != null) {
        coreTouch.move(0f, 0f);
        if (host != null) host.send();
      }
    } else if (id == dpadPointerId) {
      dpadPointerId = -1;
      releaseDpad();
    } else if (id == lookPointerId) {
      lookPointerId = -1;
      if (coreTouch != null) {
        boolean looked = coreTouch.looking();
        coreTouch.fingerUp(id);
        if (looked && host != null) host.send();
      }
    } else {
      for (PadButton b : buttons) {
        if (b.pointerId == id) {
          b.pointerId = -1;
          b.pressed = false;
          if (coreTouch != null) {
            coreTouch.action(b.action, false);
            if (host != null) host.send();
          }
        }
      }
    }
  }

  private void updateStickPosition(float x, float y) {
    float dx = x - stickCenterX;
    float dy = y - stickCenterY;
    float dist = (float) Math.hypot(dx, dy);

    if (dist > maxDisplacement) {
      dx = (dx / dist) * maxDisplacement;
      dy = (dy / dist) * maxDisplacement;
    }

    knobX = stickCenterX + dx;
    knobY = stickCenterY + dy;
    updateKnobRect();

    float normX = dx / maxDisplacement;
    float normY = dy / maxDisplacement;

    if (coreTouch != null) {
      coreTouch.move(normX, normY);
      if (host != null) host.send();
    }
  }

  private void updateDpadDirection(float x, float y) {
    float dx = x - dpadCenterX;
    float dy = y - dpadCenterY;
    float deadzone = dpadSize * 0.12f;

    boolean up = dy < -deadzone && Math.abs(dx) < Math.abs(dy) * 1.5f;
    boolean down = dy > deadzone && Math.abs(dx) < Math.abs(dy) * 1.5f;
    boolean left = dx < -deadzone && Math.abs(dy) < Math.abs(dx) * 1.5f;
    boolean right = dx > deadzone && Math.abs(dy) < Math.abs(dx) * 1.5f;

    setDpadAction(CoreTouch.UP, up, dpadUp);
    setDpadAction(CoreTouch.DOWN, down, dpadDown);
    setDpadAction(CoreTouch.LEFT, left, dpadLeft);
    setDpadAction(CoreTouch.RIGHT, right, dpadRight);

    dpadUp = up;
    dpadDown = down;
    dpadLeft = left;
    dpadRight = right;
  }

  private void setDpadAction(String action, boolean active, boolean wasActive) {
    if (active != wasActive && coreTouch != null) {
      coreTouch.action(action, active);
      if (host != null) host.send();
    }
  }

  private void releaseDpad() {
    setDpadAction(CoreTouch.UP, false, dpadUp);
    setDpadAction(CoreTouch.DOWN, false, dpadDown);
    setDpadAction(CoreTouch.LEFT, false, dpadLeft);
    setDpadAction(CoreTouch.RIGHT, false, dpadRight);
    dpadUp = dpadDown = dpadLeft = dpadRight = false;
  }

  // --- Rendering Pipeline ---
  @Override
  protected void onDraw(Canvas canvas) {
    super.onDraw(canvas);
    if (!atlas.isLoaded()) return;
    if (viewWidth <= 0 || viewHeight <= 0) {
      viewWidth = getWidth();
      viewHeight = getHeight();
      recalculateLayout();
    }

    int alpha255 = (int) (255 * currentAlpha);
    paint.setAlpha(alpha255);
    glyphPaint.setAlpha(alpha255);

    // 1. Draw Stick Ring & Knob
    Bitmap ringBmp = atlas.getStickRing();
    if (ringBmp != null) {
      canvas.drawBitmap(ringBmp, null, stickRingRect, paint);
    }
    Bitmap knobBmp = atlas.getStickKnob();
    if (knobBmp != null) {
      canvas.drawBitmap(knobBmp, null, knobRect, paint);
    }

    // 2. Draw D-Pad Cross Base
    Bitmap dpadBmp = atlas.getDpadBase();
    if (dpadBmp != null) {
      canvas.drawBitmap(dpadBmp, null, dpadRect, paint);
    }

    // 3. Draw All Buttons
    for (PadButton b : buttons) {
      boolean isHighlight = b.pressed || b.latched;
      Bitmap frameBmp = b.isCapsule
          ? (isHighlight ? atlas.getCapsulePressed() : atlas.getCapsuleNormal())
          : (isHighlight ? atlas.getButtonPressed() : atlas.getButtonNormal());

      if (frameBmp != null) {
        canvas.drawBitmap(frameBmp, null, b.bounds, paint);
      }

      // Draw Glyph
      Bitmap glyphBmp = atlas.getGlyph(b.action);
      if (glyphBmp != null) {
        float gw = b.bounds.width() * 0.48f;
        float gh = gw * ((float) glyphBmp.getHeight() / glyphBmp.getWidth());
        if (gh > b.bounds.height() * 0.60f) {
          gh = b.bounds.height() * 0.60f;
          gw = gh * ((float) glyphBmp.getWidth() / glyphBmp.getHeight());
        }
        float gx = b.bounds.centerX() - gw * 0.5f;
        float gy = b.bounds.centerY() - gh * 0.5f;
        RectF glyphRect = new RectF(gx, gy, gx + gw, gy + gh);
        canvas.drawBitmap(glyphBmp, null, glyphRect, glyphPaint);
      }
    }
  }
}
