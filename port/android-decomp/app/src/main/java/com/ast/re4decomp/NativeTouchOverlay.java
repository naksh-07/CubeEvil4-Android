package com.ast.re4decomp;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;

/**
 * Feral-grade native Android touch controls (TouchStyle.NATIVE) using Capcom RE4 Mobile
 * action icons + dynamic floating analog stick + right-half free camera/aim drag +
 * context-aware auto-hiding HUD driven by CoreBridge.touchContext().
 */
final class NativeTouchOverlay extends View {
  interface Host {
    void sendInput();
    void openMenu(boolean down, boolean finger);
    void onZoom(int direction, boolean down);
  }

  // Packed bitfield layout from port/whole/runtime/touch_context.cpp
  private static final long CTX_IN_GAME = 1L << 0;
  private static final long CTX_IN_SUBSCREEN = 1L << 1;
  private static final long CTX_IN_CUTSCENE = 1L << 2;
  private static final long CTX_AIMING_GUN = 1L << 3;
  private static final long CTX_AIMING_KNIFE = 1L << 4;
  private static final long CTX_SCOPE_BINO = 1L << 5;
  private static final long CTX_CAN_RELOAD = 1L << 6;
  private static final long CTX_ASHLEY_PRESENT = 1L << 7;
  private static final long CTX_ACT_ACTIVE = 1L << 8;
  private static final long CTX_IS_QTE = 1L << 9;
  private static final long CTX_HAS_WEAPON = 1L << 10;
  private static final long CTX_QUICK_SLOTS = 1L << 40;
  private static final long CTX_IS_MERCHANT = 1L << 41;
  private static final long CTX_IN_TYPEWRITER = 1L << 42;
  private static final long CTX_IN_DIEDEMO = 1L << 43;

  // GameCube DISP_FLAG (include/act_btn.h)
  private static final int DISP_A_NORMAL = 1;
  private static final int DISP_A_RAPID = 2;
  private static final int DISP_L_R = 3;
  private static final int DISP_A_B = 4;
  private static final int DISP_B = 5;
  private static final int DISP_X = 6;
  private static final int DISP_Y = 7;
  private static final int DISP_Z = 8;
  private static final int DISP_L = 9;
  private static final int DISP_R = 10;
  private static final int DISP_GACHA = 11;
  private static final int DISP_ROTATE = 12;
  private static final int DISP_B_RAPID = 13;
  private static final int DISP_A_ACCENT = 14;
  private static final int DISP_STICK_A = 15;
  private static final int DISP_STICK_UP = 16;

  // GameCube ACTION_TYPE (include/act_btn.h)
  private static final int ACT_TALK = 0;
  private static final int ACT_CHECK = 1;
  private static final int ACT_JUMP_OUT = 2;
  private static final int ACT_JUMP_IN = 3;
  private static final int ACT_JUMP_DOWN = 4;
  private static final int ACT_JUMP_OVER = 5;
  private static final int ACT_PUSH = 6;
  private static final int ACT_KICK = 7;
  private static final int ACT_GO_UP = 8;
  private static final int ACT_GET_DOWN = 9;
  private static final int ACT_LOOK = 13;
  private static final int ACT_PEEP = 14;
  private static final int ACT_OPEN = 16;
  private static final int ACT_JUMP_BACK = 18;
  private static final int ACT_OPERATION = 20;
  private static final int ACT_CLIMB = 25;
  private static final int ACT_JUMP = 26;
  private static final int ACT_SLIDE_DOWN = 27;
  private static final int ACT_CATCH = 28;
  private static final int ACT_PULL_UP = 29;
  private static final int ACT_HIDE = 38;
  private static final int ACT_THROUGH = 39;
  private static final int ACT_PICK_UP = 40;
  private static final int ACT_ROTATE = 42;
  private static final int ACT_SUPLEX = 44;
  private static final int ACT_SAVE = 47;
  private static final int ACT_ANSWER = 54;
  private static final int ACT_SENPUU = 56;
  private static final int ACT_BACKKICK = 57;
  private static final int ACT_EXECUTE = 59;
  private static final int ACT_PALM_SHOCK = 60;
  private static final int ACT_JUMP_MOVE = 62;

  private static final int BTN_AIM = 0;
  private static final int BTN_FIRE = 1;
  private static final int BTN_KNIFE = 2;
  private static final int BTN_RELOAD = 3;
  private static final int BTN_EVENT = 4;
  private static final int BTN_ASHLEY = 5;
  private static final int BTN_SCOPE_IN = 6;
  private static final int BTN_SCOPE_OUT = 7;
  private static final int BTN_BAG = 8;
  private static final int BTN_MAP = 9;
  private static final int BTN_PAUSE = 10;
  private static final int BTN_MENU = 11;
  private static final int BTN_QS_LEFT = 12;
  private static final int BTN_QS_UP = 13;
  private static final int BTN_QS_RIGHT = 14;
  private static final int BTN_QS_DOWN = 15;
  private static final int BTN_NAV_LEFT = 16;
  private static final int BTN_NAV_UP = 17;
  private static final int BTN_NAV_RIGHT = 18;
  private static final int BTN_NAV_DOWN = 19;
  private static final int BTN_CONFIRM = 20;
  private static final int BTN_CANCEL = 21;
  private static final int BTN_SUB_L = 22;
  private static final int BTN_SUB_R = 23;
  private static final int BTN_COUNT = 24;

  private static final long TAP_THRESHOLD_MS = 240L;
  private static final long LOOK_ZERO_DELAY_MS = 45L;
  private static final float SPRINT_ENTER_RADIUS = 0.84f;
  private static final float SPRINT_EXIT_RADIUS = 0.70f;
  private static final float STICK_ZONE_SPLIT = 0.45f;

  private static final class PadButton {
    final int id;
    float cx, cy, r;
    float halfW, halfH;
    boolean isPill;
    boolean visible;
    boolean active;
    int pointerId = -1;
    long downTimeMs;
    float downX, downY, lastX, lastY;
    boolean dragged;

    PadButton(int id, boolean isPill) {
      this.id = id;
      this.isPill = isPill;
    }

    boolean contains(float x, float y) {
      if (!visible) return false;
      if (isPill) {
        return Math.abs(x - cx) <= halfW && Math.abs(y - cy) <= halfH;
      }
      float dx = x - cx, dy = y - cy;
      return dx * dx + dy * dy <= r * r;
    }
  }

  private final CoreInput input;
  private final Host host;
  private final float density;
  private final float dragSlopPx;
  private final Map<String, Bitmap> icons = new HashMap<>();
  private final PadButton[] buttons = new PadButton[BTN_COUNT];

  private int[] safeInsets = new int[4];
  private boolean overlayShown = false;

  // Decoded game context
  private long lastCtx = -1L;
  private boolean inGame;
  private boolean inSubscreen;
  private boolean inCutscene;
  private boolean isAimingGun;
  private boolean isAimingKnife;
  private boolean inScopeOrBino;
  private boolean canReload;
  private boolean ashleyPresent;
  private boolean actBtnActive;
  private boolean isQte;
  private boolean hasWeapon;
  private boolean hasQuickSlots;
  private boolean isMerchant;
  private boolean inTypewriter;
  private boolean inDiedemo;
  private int actDisp;
  private int actKind;
  private int ammoCount;
  private int zoomDirActive;

  // Smooth HUD alpha fade
  private float combatAlpha = 1f;
  private long lastAnimMs;

  // Left floating stick state
  private int stickPid = -1;
  private float stickDefaultX, stickDefaultY;
  private float stickOriginX, stickOriginY;
  private float stickKnobX, stickKnobY;
  private float stickRadius;
  private boolean sprintActive;

  // Right look / aim drag state
  private int lookPid = -1;
  private float lookLastX, lookLastY;
  private long lookLastEventMs;

  // Hybrid tap/hold aim & knife latches
  private boolean aimLatched;
  private boolean aimWasLatchedAtDown;
  private int eventHeldBits;
  private long slashPulseUntilMs;

  // Reusable paints & rects (zero allocations in onDraw)
  private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint subTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint iconPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
  private final RectF tmpRect = new RectF();
  private final RectF iconRect = new RectF();
  private final Paint.FontMetrics tmpFm = new Paint.FontMetrics();

  private final Runnable lookZeroTask = new Runnable() {
    @Override public void run() {
      input.touchLook(0f, 0f, 0f, 0f);
      host.sendInput();
    }
  };

  NativeTouchOverlay(Context context, CoreInput input, Host host) {
    super(context);
    this.input = input;
    this.host = host;
    this.density = context.getResources().getDisplayMetrics().density;
    this.dragSlopPx = Math.max(12f * density, ViewConfiguration.get(context).getScaledTouchSlop());

    for (int i = 0; i < BTN_COUNT; i++) {
      boolean pill = i == BTN_BAG || i == BTN_MAP || i == BTN_PAUSE || i == BTN_MENU
          || (i >= BTN_QS_LEFT && i <= BTN_COUNT - 1);
      buttons[i] = new PadButton(i, pill);
    }

    ringPaint.setStyle(Paint.Style.STROKE);
    glowPaint.setStyle(Paint.Style.STROKE);
    textPaint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
    textPaint.setTextAlign(Paint.Align.CENTER);
    subTextPaint.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD));
    subTextPaint.setTextAlign(Paint.Align.CENTER);

    loadIcons(context);
  }

  private void loadIcons(Context context) {
    String[] names = {
        "vpad_knob_34x34.png",
        "actbtn_00_stop_cancel.png", "actbtn_02_ok_check.png", "actbtn_06_gun_aim.png",
        "actbtn_07_knife.png", "actbtn_08_run_sprint.png", "actbtn_09_scope_1x.png",
        "actbtn_10_scope_2x.png", "actbtn_15_ashley_cmd.png", "actbtn_16_pickup_hand.png",
        "actbtn_17_examine_excl.png", "actbtn_18_door_open.png", "actbtn_19_climb_up.png",
        "actbtn_20_jump_down.png", "actbtn_21_ladder_up.png", "actbtn_22_ladder_down.png",
        "actbtn_23_qte_mash.png", "actbtn_24_catch_ashley.png", "actbtn_25_lever_crate.png",
        "actbtn_26_jump_gap.png", "actbtn_27_kick_roundhouse.png", "actbtn_28_melee_suplex.png",
        "actbtn_29_knife_prompt.png", "actbtn_30_speak_window.png", "actbtn_31_hide_bin.png",
        "actbtn_32_dodge_lr.png"
    };
    for (String name : names) {
      try (InputStream is = context.getAssets().open("native_touch/" + name)) {
        Bitmap bmp = BitmapFactory.decodeStream(is);
        if (bmp != null) icons.put(name, bmp);
      } catch (IOException ignored) {
      }
    }
  }

  void setSafeInsets(int[] insets) {
    if (insets == null || insets.length < 4) return;
    boolean changed = false;
    for (int i = 0; i < 4; i++) {
      if (safeInsets[i] != insets[i]) {
        safeInsets[i] = insets[i];
        changed = true;
      }
    }
    if (changed) {
      layoutControls(getWidth(), getHeight());
      invalidate();
    }
  }

  void setOverlayShown(boolean shown) {
    if (this.overlayShown == shown) return;
    this.overlayShown = shown;
    if (!shown) clearAllTouches(true);
    setVisibility(shown ? VISIBLE : GONE);
    invalidate();
  }

  void reset() {
    clearAllTouches(true);
    invalidate();
  }

  void updateContext(long ctx) {
    if (!overlayShown) return;
    boolean changed = (ctx != lastCtx);
    lastCtx = ctx;

    inGame = (ctx & CTX_IN_GAME) != 0;
    inSubscreen = (ctx & CTX_IN_SUBSCREEN) != 0;
    inCutscene = (ctx & CTX_IN_CUTSCENE) != 0;
    isAimingGun = (ctx & CTX_AIMING_GUN) != 0;
    isAimingKnife = (ctx & CTX_AIMING_KNIFE) != 0;
    inScopeOrBino = (ctx & CTX_SCOPE_BINO) != 0;
    canReload = (ctx & CTX_CAN_RELOAD) != 0;
    ashleyPresent = (ctx & CTX_ASHLEY_PRESENT) != 0;
    actBtnActive = (ctx & CTX_ACT_ACTIVE) != 0;
    isQte = (ctx & CTX_IS_QTE) != 0;
    hasWeapon = (ctx & CTX_HAS_WEAPON) != 0;
    hasQuickSlots = (ctx & CTX_QUICK_SLOTS) != 0;
    isMerchant = (ctx & CTX_IS_MERCHANT) != 0;
    inTypewriter = (ctx & CTX_IN_TYPEWRITER) != 0;
    inDiedemo = (ctx & CTX_IN_DIEDEMO) != 0;
    actDisp = (int) ((ctx >>> 11) & 0x1FL);
    actKind = (int) ((ctx >>> 16) & 0xFFL);
    ammoCount = (int) ((ctx >>> 32) & 0xFFL);

    if (!inGame || inSubscreen || inCutscene || !hasWeapon) {
      if (aimLatched) {
        aimLatched = false;
        input.touchButton(CoreInput.GC_R, false);
        host.sendInput();
      }
    }
    if (!inGame || inSubscreen || inCutscene) {
      releaseNonCombatInputs(!inGame || inSubscreen || !needsStickInCutscene(), !inGame || inSubscreen);
    }

    if (changed) {
      updateButtonVisibility();
      releaseInvisibleButtons();
      invalidate();
    } else if (isQte || SystemClock.uptimeMillis() < slashPulseUntilMs) {
      invalidate();
    }
  }

  private boolean needsStickInCutscene() {
    return actBtnActive && (actDisp == DISP_ROTATE || actDisp == DISP_STICK_A || actDisp == DISP_STICK_UP);
  }

  private void updateButtonVisibility() {
    boolean combatUi = inGame && !inSubscreen && !inCutscene;
    buttons[BTN_MENU].visible = true;
    buttons[BTN_PAUSE].visible = !inSubscreen;
    buttons[BTN_BAG].visible = combatUi;
    buttons[BTN_MAP].visible = combatUi;

    buttons[BTN_AIM].visible = combatUi && (hasWeapon || inScopeOrBino);
    buttons[BTN_FIRE].visible = combatUi && (isAimingGun || isAimingKnife || inScopeOrBino);
    buttons[BTN_KNIFE].visible = combatUi && !inScopeOrBino;
    buttons[BTN_RELOAD].visible = combatUi && canReload && !inScopeOrBino;
    buttons[BTN_ASHLEY].visible = combatUi && ashleyPresent && !inScopeOrBino;
    buttons[BTN_SCOPE_IN].visible = combatUi && inScopeOrBino;
    buttons[BTN_SCOPE_OUT].visible = combatUi && inScopeOrBino;
    buttons[BTN_EVENT].visible = inGame && !inSubscreen && actBtnActive
        && (!inCutscene || isQte || needsStickInCutscene());

    boolean showQs = combatUi && hasQuickSlots && !isAimingGun && !inScopeOrBino;
    for (int i = BTN_QS_LEFT; i <= BTN_QS_DOWN; i++) {
      buttons[i].visible = showQs;
    }

    // Menu / Typewriter / Merchant / Death-Continue / Subscreen navigation so screens never require a controller
    boolean menuNav = !inGame || inTypewriter || isMerchant || inDiedemo;
    for (int i = BTN_NAV_LEFT; i <= BTN_NAV_DOWN; i++) {
      buttons[i].visible = menuNav || inSubscreen;
    }
    buttons[BTN_CONFIRM].visible = menuNav || inSubscreen;
    buttons[BTN_CANCEL].visible = menuNav || inSubscreen;
    boolean subRotate = inGame && inSubscreen && !isMerchant && !inTypewriter;
    buttons[BTN_SUB_L].visible = subRotate;
    buttons[BTN_SUB_R].visible = subRotate;
  }

  @Override
  protected void onSizeChanged(int w, int h, int oldw, int oldh) {
    super.onSizeChanged(w, h, oldw, oldh);
    layoutControls(w, h);
  }

  private void layoutControls(int w, int h) {
    if (w <= 0 || h <= 0) return;
    float sL = safeInsets[0], sT = safeInsets[1], sR = safeInsets[2], sB = safeInsets[3];
    float safeW = Math.max(1f, w - sL - sR);
    float safeH = Math.max(1f, h - sT - sB);
    float u = Math.min(safeW, safeH);
    float margin = Math.max(14f * density, u * 0.035f);

    // Floating stick geometry
    stickRadius = Math.max(54f * density, Math.min(82f * density, u * 0.165f));
    stickDefaultX = sL + margin + stickRadius * 1.15f;
    stickDefaultY = h - sB - margin - stickRadius * 1.05f;
    if (stickPid < 0) {
      stickOriginX = stickKnobX = stickDefaultX;
      stickOriginY = stickKnobY = stickDefaultY;
    }

    // Top-left utility pills (Bag, Map)
    float pillW = Math.max(46f * density, u * 0.115f);
    float pillH = Math.max(30f * density, u * 0.072f);
    float topY = sT + margin + pillH * 0.5f;
    setPill(BTN_BAG, sL + margin + pillW * 0.5f, topY, pillW * 0.5f, pillH * 0.5f);
    setPill(BTN_MAP, buttons[BTN_BAG].cx + pillW + 8f * density, topY, pillW * 0.5f, pillH * 0.5f);
    // Attaché Case / Map L & R rotate/zoom pills share the hidden BAG & MAP positions during subscreens
    setPill(BTN_SUB_L, buttons[BTN_BAG].cx, topY, pillW * 0.5f, pillH * 0.5f);
    setPill(BTN_SUB_R, buttons[BTN_MAP].cx, topY, pillW * 0.5f, pillH * 0.5f);

    // Top-right Menu (Settings) & Pause/Skip
    float smallPillW = Math.max(36f * density, u * 0.09f);
    setPill(BTN_MENU, w - sR - margin - smallPillW * 0.5f, topY, smallPillW * 0.5f, pillH * 0.5f);
    setPill(BTN_PAUSE, buttons[BTN_MENU].cx - smallPillW * 0.5f - pillW * 0.5f - 8f * density, topY,
        pillW * 0.5f, pillH * 0.5f);

    // Right-hand thumb arc (Feral + Capcom ergonomics), keeping bottom-right RE4 Life/Ammo ring clear
    float rFire = Math.max(33f * density, Math.min(43f * density, u * 0.088f));
    float rAim = Math.max(29f * density, Math.min(37f * density, u * 0.076f));
    float rMed = Math.max(24f * density, Math.min(30f * density, u * 0.062f));
    float rEvent = Math.max(30f * density, Math.min(39f * density, u * 0.080f));

    float rightEdge = w - sR - margin;
    float bottomEdge = h - sB - margin;

    // Aim sits comfortably left of the bottom-right Life/Ammo ring (~0.27*u); Fire sits above-left of the ring
    setCircle(BTN_AIM, rightEdge - u * 0.46f, bottomEdge - rAim * 1.10f, rAim);
    setCircle(BTN_FIRE, rightEdge - u * 0.34f, bottomEdge - u * 0.34f, rFire);
    // Knife sits cleanly inboard left of Aim
    setCircle(BTN_KNIFE, buttons[BTN_AIM].cx - rAim - rMed - 14f * density,
        bottomEdge - rMed * 1.15f, rMed);
    // Reload floats neatly left of Fire
    setCircle(BTN_RELOAD, buttons[BTN_FIRE].cx - rFire - rMed - 14f * density,
        buttons[BTN_FIRE].cy, rMed);
    // Contextual Action / QTE button sits cleanly above the Life/Ammo ring with zero overlap
    setCircle(BTN_EVENT, rightEdge - rEvent * 1.05f,
        bottomEdge - u * 0.44f - rEvent * 1.05f, rEvent);
    // Ashley command sits cleanly above Knife
    setCircle(BTN_ASHLEY, buttons[BTN_KNIFE].cx,
        buttons[BTN_KNIFE].cy - rMed * 2.45f, rMed * 0.92f);

    // Scope zoom +/- reuse Reload & Ashley slots during Sniper/Binoculars (zero overlap with BTN_EVENT)
    setCircle(BTN_SCOPE_IN, buttons[BTN_RELOAD].cx, buttons[BTN_RELOAD].cy, rMed);
    setCircle(BTN_SCOPE_OUT, buttons[BTN_ASHLEY].cx, buttons[BTN_ASHLEY].cy, rMed);

    // Top-center quick-weapon strip (leaves bottom-center 100% clear for RE4 action prompts & subtitles!)
    float qsW = Math.max(34f * density, u * 0.078f);
    float qsH = pillH;
    float qsGap = 6f * density;
    float qsTotalW = 4f * qsW + 3f * qsGap;
    float qsStart = w * 0.5f - qsTotalW * 0.5f + qsW * 0.5f;
    float qsY = topY;
    for (int i = 0; i < 4; i++) {
      setPill(BTN_QS_LEFT + i, qsStart + i * (qsW + qsGap), qsY, qsW * 0.5f, qsH * 0.5f);
    }

    // Compact Menu/Typewriter D-pad bottom-left + OK/BACK pills bottom-right (visible only in menus/subscreens)
    float navCell = Math.max(40f * density, u * 0.095f);
    float navPadX = sL + margin + navCell * 1.6f;
    float navPadY = bottomEdge - navCell * 1.45f;
    setPill(BTN_NAV_LEFT, navPadX - navCell, navPadY, navCell * 0.45f, navCell * 0.45f);
    setPill(BTN_NAV_RIGHT, navPadX + navCell, navPadY, navCell * 0.45f, navCell * 0.45f);
    setPill(BTN_NAV_UP, navPadX, navPadY - navCell * 0.92f, navCell * 0.45f, navCell * 0.45f);
    setPill(BTN_NAV_DOWN, navPadX, navPadY + navCell * 0.92f, navCell * 0.45f, navCell * 0.45f);

    float actPillW = Math.max(66f * density, u * 0.16f);
    float actPillH = Math.max(38f * density, u * 0.088f);
    setPill(BTN_CONFIRM, rightEdge - actPillW * 0.5f, bottomEdge - actPillH * 0.5f, actPillW * 0.5f, actPillH * 0.5f);
    setPill(BTN_CANCEL, rightEdge - actPillW * 1.65f, bottomEdge - actPillH * 0.5f, actPillW * 0.5f, actPillH * 0.5f);

    updateButtonVisibility();
  }

  private void setCircle(int idx, float cx, float cy, float r) {
    buttons[idx].cx = cx;
    buttons[idx].cy = cy;
    buttons[idx].r = r;
  }

  private void setPill(int idx, float cx, float cy, float hw, float hh) {
    buttons[idx].cx = cx;
    buttons[idx].cy = cy;
    buttons[idx].halfW = hw;
    buttons[idx].halfH = hh;
  }

  /**
   * Used by CoreActivity.watchTap so touches belonging to NativeTouchOverlay controls
   * do not double-fire CoreBridge.tap / touchDown / knifeSwipe / swipe.
   */
  boolean onControl(float x, float y) {
    if (!overlayShown) return false;
    if (hitButton(x, y) != null) return true;
    if (!inGame || inSubscreen) return false;
    if (inCutscene) return needsStickInCutscene() && isLeftStickZone(x, y);
    return isLeftStickZone(x, y) || isLookZone(x, y);
  }

  private boolean isLeftStickZone(float x, float y) {
    float topGuard = safeInsets[1] + 58f * density;
    return x < getWidth() * STICK_ZONE_SPLIT && y > topGuard;
  }

  private boolean isLookZone(float x, float y) {
    float topGuard = safeInsets[1] + 58f * density;
    return x >= getWidth() * STICK_ZONE_SPLIT && y > topGuard;
  }

  private PadButton hitButton(float x, float y) {
    // Priority order matches visual stacking & combat urgency
    int[] order = {
        BTN_MENU, BTN_PAUSE, BTN_EVENT, BTN_FIRE, BTN_AIM, BTN_RELOAD, BTN_KNIFE,
        BTN_SCOPE_IN, BTN_SCOPE_OUT, BTN_ASHLEY, BTN_BAG, BTN_MAP, BTN_SUB_L, BTN_SUB_R,
        BTN_QS_LEFT, BTN_QS_UP, BTN_QS_RIGHT, BTN_QS_DOWN,
        BTN_CONFIRM, BTN_CANCEL, BTN_NAV_LEFT, BTN_NAV_UP, BTN_NAV_RIGHT, BTN_NAV_DOWN
    };
    for (int id : order) {
      if (buttons[id].contains(x, y)) return buttons[id];
    }
    return null;
  }

  @Override
  public boolean onTouchEvent(MotionEvent e) {
    if (!overlayShown) return false;
    int action = e.getActionMasked();
    int actionIdx = e.getActionIndex();

    switch (action) {
      case MotionEvent.ACTION_DOWN: {
        int pid = e.getPointerId(actionIdx);
        handlePointerDown(pid, e.getX(actionIdx), e.getY(actionIdx), e.getEventTime());
        // Always claim primary touch target so multi-finger ACTION_POINTER_DOWN (stick/QTE/OK/BACK)
        // is never withheld by the background SurfaceView listener when finger 0 lands outside a zone.
        // CoreActivity.watchTap() runs earlier in dispatchTouchEvent and checks onControl(x, y) itself.
        return true;
      }
      case MotionEvent.ACTION_POINTER_DOWN: {
        int pid = e.getPointerId(actionIdx);
        float x = e.getX(actionIdx);
        float y = e.getY(actionIdx);
        return handlePointerDown(pid, x, y, e.getEventTime());
      }
      case MotionEvent.ACTION_MOVE: {
        boolean consumed = false;
        long now = e.getEventTime();
        for (int i = 0; i < e.getPointerCount(); i++) {
          int pid = e.getPointerId(i);
          if (handlePointerMove(pid, e.getX(i), e.getY(i), now)) {
            consumed = true;
          }
        }
        if (consumed) {
          host.sendInput();
          invalidate();
        }
        return consumed || stickPid >= 0 || lookPid >= 0 || anyButtonHeld();
      }
      case MotionEvent.ACTION_UP:
      case MotionEvent.ACTION_POINTER_UP: {
        int pid = e.getPointerId(actionIdx);
        boolean released = handlePointerUp(pid, e.getEventTime());
        if (released) {
          host.sendInput();
          invalidate();
        }
        return released;
      }
      case MotionEvent.ACTION_CANCEL:
        clearAllTouches(true);
        invalidate();
        return true;
      default:
        return false;
    }
  }

  private boolean handlePointerDown(int pid, float x, float y, long eventTimeMs) {
    PadButton btn = hitButton(x, y);
    if (btn != null && btn.pointerId < 0) {
      btn.pointerId = pid;
      btn.active = true;
      btn.downTimeMs = eventTimeMs;
      btn.downX = btn.lastX = x;
      btn.downY = btn.lastY = y;
      btn.dragged = false;
      onButtonDown(btn);
      performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
      host.sendInput();
      invalidate();
      return true;
    }

    if (inGame && !inSubscreen && (!inCutscene || needsStickInCutscene())
        && stickPid < 0 && isLeftStickZone(x, y)) {
      stickPid = pid;
      anchorFloatingStick(x, y);
      updateStickFromTouch(x, y);
      host.sendInput();
      invalidate();
      return true;
    }

    if (inGame && !inSubscreen && !inCutscene && lookPid < 0 && isLookZone(x, y)) {
      lookPid = pid;
      lookLastX = x;
      lookLastY = y;
      lookLastEventMs = eventTimeMs;
      removeCallbacks(lookZeroTask);
      invalidate();
      return true;
    }
    return false;
  }

  private boolean handlePointerMove(int pid, float x, float y, long nowMs) {
    if (pid == stickPid) {
      updateStickFromTouch(x, y);
      return true;
    }
    if (pid == lookPid) {
      float dx = x - lookLastX;
      float dy = y - lookLastY;
      long dt = Math.max(8L, Math.min(33L, nowMs - lookLastEventMs));
      lookLastX = x;
      lookLastY = y;
      lookLastEventMs = nowMs;
      handleLookOrAimDrag(dx, dy, dt, false);
      return true;
    }

    // Thumb-slide precision aiming while holding Aim, Fire, or Knife!
    for (int id : new int[] {BTN_FIRE, BTN_AIM, BTN_KNIFE}) {
      PadButton b = buttons[id];
      if (b.pointerId == pid) {
        float dx = x - b.lastX;
        float dy = y - b.lastY;
        b.lastX = x;
        b.lastY = y;
        if (!b.dragged && Math.hypot(x - b.downX, y - b.downY) > dragSlopPx) {
          b.dragged = true;
        }
        if (b.dragged && (isAimingGun || isAimingKnife || inScopeOrBino
            || id == BTN_AIM || id == BTN_KNIFE)) {
          handleLookOrAimDrag(dx, dy, 16L, id == BTN_AIM || id == BTN_KNIFE);
        }
        return true;
      }
    }
    return false;
  }

  private boolean handlePointerUp(int pid, long eventTimeMs) {
    if (pid == stickPid) {
      stickPid = -1;
      stickOriginX = stickKnobX = stickDefaultX;
      stickOriginY = stickKnobY = stickDefaultY;
      input.touchStick(false, 0f, 0f);
      if (sprintActive) {
        sprintActive = false;
        input.touchButton(CoreInput.GC_B, false);
      }
      return true;
    }
    if (pid == lookPid) {
      lookPid = -1;
      removeCallbacks(lookZeroTask);
      input.touchLook(0f, 0f, 0f, 0f);
      return true;
    }
    for (PadButton b : buttons) {
      if (b.pointerId == pid) {
        b.pointerId = -1;
        b.active = false;
        onButtonUp(b, eventTimeMs);
        return true;
      }
    }
    return false;
  }

  private void anchorFloatingStick(float touchX, float touchY) {
    float pad = 10f * density;
    float minX = safeInsets[0] + stickRadius + pad;
    float maxX = getWidth() * STICK_ZONE_SPLIT - stickRadius * 0.4f;
    float minY = safeInsets[1] + 58f * density + stickRadius;
    float maxY = getHeight() - safeInsets[3] - stickRadius - pad;
    stickOriginX = clamp(touchX, minX, Math.max(minX, maxX));
    stickOriginY = clamp(touchY, minY, Math.max(minY, maxY));
  }

  private void updateStickFromTouch(float x, float y) {
    float dx = x - stickOriginX;
    float dy = y - stickOriginY;
    float dist = (float) Math.hypot(dx, dy);
    float r = Math.min(1f, dist / stickRadius);
    if (dist > stickRadius && dist > 0f) {
      float scale = stickRadius / dist;
      dx *= scale;
      dy *= scale;
    }
    stickKnobX = stickOriginX + dx;
    stickKnobY = stickOriginY + dy;

    float nx = dx / stickRadius;
    float ny = dy / stickRadius;
    input.touchStick(false, nx, ny);
    if (isAimingGun || isAimingKnife || inScopeOrBino || inCutscene) {
      if (sprintActive) {
        sprintActive = false;
        input.touchButton(CoreInput.GC_B, false);
      }
    } else {
      if (!sprintActive && r >= SPRINT_ENTER_RADIUS) {
        sprintActive = true;
        input.touchButton(CoreInput.GC_B, true);
        performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK);
      } else if (sprintActive && r <= SPRINT_EXIT_RADIUS) {
        sprintActive = false;
        input.touchButton(CoreInput.GC_B, false);
      }
    }
  }

  private void handleLookOrAimDrag(float dx, float dy, long dtMs, boolean forceAimSwipe) {
    float unit = Math.max(1f, Math.min(getWidth(), getHeight()) * 0.22f);
    float movePx = (float) Math.hypot(dx, dy);
    boolean swipeMode = forceAimSwipe || isAimingGun || isAimingKnife || inScopeOrBino
        || buttons[BTN_AIM].pointerId >= 0 || buttons[BTN_KNIFE].pointerId >= 0;
    if (swipeMode) {
      removeCallbacks(lookZeroTask);
      // Non-linear precision curve: tiny thumb adjustments stay surgical; flicks track fast targets
      float accel = 1f + Math.min(1.4f, movePx / (18f * density));
      float scale = 0.65f * accel / unit;
      input.touchSwipe(dx * scale, dy * scale);
    } else {
      float lookScale = 1f / (unit * 0.32f);
      float dtNorm = 16f / dtMs;
      float lx = clamp(dx * dtNorm * lookScale, -1f, 1f);
      float ly = clamp(dy * dtNorm * lookScale, -1f, 1f);
      input.touchLook(lx, ly, 0f, 0f);
      if (input.bodyTurn()) {
        input.touchTurn((dx / unit) * 0.55f);
      }
      removeCallbacks(lookZeroTask);
      postDelayed(lookZeroTask, LOOK_ZERO_DELAY_MS);
    }
  }

  private void onButtonDown(PadButton btn) {
    switch (btn.id) {
      case BTN_AIM:
        aimWasLatchedAtDown = aimLatched;
        aimLatched = true;
        input.touchButton(CoreInput.GC_R, true);
        break;
      case BTN_FIRE:
        input.touchButton(CoreInput.GC_A, true);
        break;
      case BTN_KNIFE:
        input.touchButton(CoreInput.GC_L, true);
        break;
      case BTN_RELOAD:
        CoreBridge.requestReload();
        break;
      case BTN_EVENT:
        if (isQte) {
          CoreBridge.touchDown();
        } else {
          eventHeldBits = eventButtonBits();
          if (eventHeldBits != 0) input.touchButton(eventHeldBits, true);
        }
        break;
      case BTN_ASHLEY:
        input.touchButton(CoreInput.GC_X, true);
        break;
      case BTN_SCOPE_IN:
        zoomDirActive = 1;
        host.onZoom(1, true);
        break;
      case BTN_SCOPE_OUT:
        zoomDirActive = -1;
        host.onZoom(-1, true);
        break;
      case BTN_BAG:
        input.touchButton(CoreInput.GC_Y, true);
        break;
      case BTN_MAP:
        input.touchButton(CoreInput.GC_Z, true);
        break;
      case BTN_PAUSE:
        input.touchButton(CoreInput.GC_START, true);
        break;
      case BTN_MENU:
        host.openMenu(true, true);
        break;
      case BTN_QS_LEFT:
      case BTN_NAV_LEFT:
        input.touchButton(CoreInput.GC_LEFT, true);
        break;
      case BTN_QS_UP:
      case BTN_NAV_UP:
        input.touchButton(CoreInput.GC_UP, true);
        break;
      case BTN_QS_RIGHT:
      case BTN_NAV_RIGHT:
        input.touchButton(CoreInput.GC_RIGHT, true);
        break;
      case BTN_QS_DOWN:
      case BTN_NAV_DOWN:
        input.touchButton(CoreInput.GC_DOWN, true);
        break;
      case BTN_CONFIRM:
        input.touchButton(CoreInput.GC_A, true);
        break;
      case BTN_CANCEL:
        input.touchButton(CoreInput.GC_B, true);
        break;
      case BTN_SUB_L:
        input.touchButton(CoreInput.GC_L, true);
        break;
      case BTN_SUB_R:
        input.touchButton(CoreInput.GC_R, true);
        break;
      default:
        break;
    }
  }

  private void onButtonUp(PadButton btn, long eventTimeMs) {
    long heldMs = eventTimeMs - btn.downTimeMs;
    switch (btn.id) {
      case BTN_AIM:
        removeCallbacks(lookZeroTask);
        // Hybrid Tap/Hold:
        // - Short clean tap (<240ms and not dragged) toggles Aim latch ON/OFF
        // - Hold (>=240ms or dragged to aim) releases Aim immediately on finger lift
        if (heldMs < TAP_THRESHOLD_MS && !btn.dragged) {
          aimLatched = !aimWasLatchedAtDown;
        } else {
          aimLatched = false;
        }
        input.touchButton(CoreInput.GC_R, aimLatched);
        break;
      case BTN_FIRE:
        input.touchButton(CoreInput.GC_A, false);
        break;
      case BTN_KNIFE:
        removeCallbacks(lookZeroTask);
        input.touchButton(CoreInput.GC_L, false);
        if (heldMs < TAP_THRESHOLD_MS && !btn.dragged && !isAimingKnife) {
          CoreBridge.slash(0f);
          slashPulseUntilMs = SystemClock.uptimeMillis() + 180L;
        }
        break;
      case BTN_EVENT:
        if (eventHeldBits != 0) {
          input.touchButton(eventHeldBits, false);
          eventHeldBits = 0;
        }
        break;
      case BTN_ASHLEY:
        input.touchButton(CoreInput.GC_X, false);
        break;
      case BTN_SCOPE_IN:
        if (zoomDirActive == 1) zoomDirActive = 0;
        host.onZoom(1, false);
        break;
      case BTN_SCOPE_OUT:
        if (zoomDirActive == -1) zoomDirActive = 0;
        host.onZoom(-1, false);
        break;
      case BTN_BAG:
        input.touchButton(CoreInput.GC_Y, false);
        break;
      case BTN_MAP:
        input.touchButton(CoreInput.GC_Z, false);
        break;
      case BTN_PAUSE:
        input.touchButton(CoreInput.GC_START, false);
        break;
      case BTN_MENU:
        host.openMenu(false, true);
        break;
      case BTN_QS_LEFT:
      case BTN_NAV_LEFT:
        input.touchButton(CoreInput.GC_LEFT, false);
        break;
      case BTN_QS_UP:
      case BTN_NAV_UP:
        input.touchButton(CoreInput.GC_UP, false);
        break;
      case BTN_QS_RIGHT:
      case BTN_NAV_RIGHT:
        input.touchButton(CoreInput.GC_RIGHT, false);
        break;
      case BTN_QS_DOWN:
      case BTN_NAV_DOWN:
        input.touchButton(CoreInput.GC_DOWN, false);
        break;
      case BTN_CONFIRM:
        input.touchButton(CoreInput.GC_A, false);
        break;
      case BTN_CANCEL:
        input.touchButton(CoreInput.GC_B, false);
        break;
      case BTN_SUB_L:
        input.touchButton(CoreInput.GC_L, false);
        break;
      case BTN_SUB_R:
        input.touchButton(CoreInput.GC_R, false);
        break;
      default:
        break;
    }
  }

  private int eventButtonBits() {
    switch (actDisp) {
      case DISP_B:
      case DISP_B_RAPID:
        return CoreInput.GC_B;
      case DISP_X:
        return CoreInput.GC_X;
      case DISP_Y:
        return CoreInput.GC_Y;
      case DISP_Z:
        return CoreInput.GC_Z;
      case DISP_L:
        return CoreInput.GC_L;
      case DISP_R:
        return CoreInput.GC_R;
      default:
        return CoreInput.GC_A;
    }
  }

  private boolean anyButtonHeld() {
    for (PadButton b : buttons) if (b.pointerId >= 0) return true;
    return false;
  }

  private void releaseNonCombatInputs(boolean releaseStick, boolean releasePauseAndEvent) {
    boolean changed = false;
    if (releaseStick && stickPid >= 0) {
      stickPid = -1;
      stickOriginX = stickKnobX = stickDefaultX;
      stickOriginY = stickKnobY = stickDefaultY;
      input.touchStick(false, 0f, 0f);
      changed = true;
    }
    if (lookPid >= 0) {
      lookPid = -1;
      removeCallbacks(lookZeroTask);
      input.touchLook(0f, 0f, 0f, 0f);
      changed = true;
    }
    if (sprintActive) {
      sprintActive = false;
      input.touchButton(CoreInput.GC_B, false);
      changed = true;
    }
    if (zoomDirActive != 0) {
      host.onZoom(zoomDirActive, false);
      zoomDirActive = 0;
      changed = true;
    }
    int[] combatOnlyBtns = {
        BTN_AIM, BTN_FIRE, BTN_KNIFE, BTN_RELOAD, BTN_ASHLEY,
        BTN_SCOPE_IN, BTN_SCOPE_OUT, BTN_BAG, BTN_MAP,
        BTN_QS_LEFT, BTN_QS_UP, BTN_QS_RIGHT, BTN_QS_DOWN
    };
    for (int id : combatOnlyBtns) {
      PadButton b = buttons[id];
      if (b.pointerId >= 0 || b.active) {
        b.pointerId = -1;
        b.active = false;
        int bit = buttonGcBit(id);
        if (bit != 0) input.touchButton(bit, false);
        changed = true;
      }
    }
    if (releasePauseAndEvent) {
      if (eventHeldBits != 0) {
        input.touchButton(eventHeldBits, false);
        eventHeldBits = 0;
        changed = true;
      }
      for (int id : new int[] {BTN_EVENT, BTN_PAUSE}) {
        PadButton b = buttons[id];
        if (b.pointerId >= 0 || b.active) {
          b.pointerId = -1;
          b.active = false;
          int bit = buttonGcBit(id);
          if (bit != 0) input.touchButton(bit, false);
          changed = true;
        }
      }
    }
    if (changed) {
      host.sendInput();
      invalidate();
    }
  }

  private void releaseInvisibleButtons() {
    boolean changed = false;
    for (PadButton b : buttons) {
      if (!b.visible && (b.pointerId >= 0 || b.active)) {
        b.pointerId = -1;
        b.active = false;
        if (b.id == BTN_EVENT && eventHeldBits != 0) {
          input.touchButton(eventHeldBits, false);
          eventHeldBits = 0;
        } else {
          int bit = buttonGcBit(b.id);
          if (bit != 0) input.touchButton(bit, false);
        }
        changed = true;
      }
    }
    if (changed) host.sendInput();
  }

  private int buttonGcBit(int id) {
    switch (id) {
      case BTN_AIM:
      case BTN_SUB_R:
        return CoreInput.GC_R;
      case BTN_FIRE:
      case BTN_CONFIRM:
        return CoreInput.GC_A;
      case BTN_KNIFE:
      case BTN_SUB_L:
        return CoreInput.GC_L;
      case BTN_ASHLEY: return CoreInput.GC_X;
      case BTN_BAG: return CoreInput.GC_Y;
      case BTN_MAP: return CoreInput.GC_Z;
      case BTN_PAUSE: return CoreInput.GC_START;
      case BTN_CANCEL: return CoreInput.GC_B;
      case BTN_QS_LEFT:
      case BTN_NAV_LEFT:
        return CoreInput.GC_LEFT;
      case BTN_QS_UP:
      case BTN_NAV_UP:
        return CoreInput.GC_UP;
      case BTN_QS_RIGHT:
      case BTN_NAV_RIGHT:
        return CoreInput.GC_RIGHT;
      case BTN_QS_DOWN:
      case BTN_NAV_DOWN:
        return CoreInput.GC_DOWN;
      default: return 0;
    }
  }

  private void clearAllTouches(boolean releaseInput) {
    removeCallbacks(lookZeroTask);
    stickPid = -1;
    lookPid = -1;
    stickOriginX = stickKnobX = stickDefaultX;
    stickOriginY = stickKnobY = stickDefaultY;
    sprintActive = false;
    aimLatched = false;
    eventHeldBits = 0;
    if (zoomDirActive != 0) {
      host.onZoom(zoomDirActive, false);
      zoomDirActive = 0;
    }
    for (PadButton b : buttons) {
      if (b.id == BTN_MENU && b.pointerId >= 0) host.openMenu(false, false);
      b.pointerId = -1;
      b.active = false;
    }
    if (releaseInput) {
      input.touchClear();
      host.sendInput();
    }
  }

  @Override
  protected void onDraw(Canvas canvas) {
    if (!overlayShown) return;
    long now = SystemClock.uptimeMillis();
    float dt = lastAnimMs == 0 ? 16f : Math.min(50f, Math.max(1f, now - lastAnimMs));
    lastAnimMs = now;

    float targetAlpha = (inGame && !inSubscreen && !inCutscene) ? 1f : 0f;
    if (Math.abs(combatAlpha - targetAlpha) > 0.01f) {
      float step = dt / 160f;
      combatAlpha = combatAlpha < targetAlpha
          ? Math.min(targetAlpha, combatAlpha + step)
          : Math.max(targetAlpha, combatAlpha - step);
      postInvalidateOnAnimation();
    } else {
      combatAlpha = targetAlpha;
    }

    // Floating Left Stick (also visible in wiggle/rotate cutscenes)
    if (combatAlpha > 0.02f || (inGame && !inSubscreen && needsStickInCutscene())) {
      float stickAlphaScale = Math.max(combatAlpha, needsStickInCutscene() ? 1f : 0f);
      drawFloatingStick(canvas, stickAlphaScale);
    }

    // Subtle Settings Gear (always accessible)
    drawPillButton(canvas, buttons[BTN_MENU], "⚙", null, false,
        buttons[BTN_MENU].active ? 0.92f : (inGame && !inSubscreen && !inCutscene ? 0.65f : 0.30f));

    // Pause / Skip / Start pill
    if (buttons[BTN_PAUSE].visible) {
      String pauseLbl = !inGame ? "START" : (inCutscene ? "SKIP" : "PAUSE");
      drawPillButton(canvas, buttons[BTN_PAUSE], pauseLbl, null, false,
          buttons[BTN_PAUSE].active ? 0.95f : (!inGame || inCutscene ? 0.45f : 0.72f));
    }

    if (combatAlpha > 0.02f) {
      if (buttons[BTN_BAG].visible) {
        drawPillButton(canvas, buttons[BTN_BAG], "CASE", null, false, combatAlpha * 0.78f);
      }
      if (buttons[BTN_MAP].visible) {
        drawPillButton(canvas, buttons[BTN_MAP], "MAP", null, false, combatAlpha * 0.78f);
      }
      if (buttons[BTN_AIM].visible) {
        boolean aimGlow = isAimingGun || aimLatched;
        drawCircleActionButton(canvas, buttons[BTN_AIM], "actbtn_06_gun_aim.png",
            aimLatched ? "LOCKED" : "AIM", aimGlow, false, combatAlpha);
      }
      if (buttons[BTN_FIRE].visible) {
        drawCircleActionButton(canvas, buttons[BTN_FIRE],
            isAimingKnife && !isAimingGun ? "actbtn_29_knife_prompt.png" : "actbtn_06_gun_aim.png",
            isAimingKnife && !isAimingGun ? "SLASH" : "FIRE",
            buttons[BTN_FIRE].active || isAimingGun, true, combatAlpha);
      }
      if (buttons[BTN_KNIFE].visible) {
        boolean knifeGlow = isAimingKnife || now < slashPulseUntilMs;
        drawCircleActionButton(canvas, buttons[BTN_KNIFE], "actbtn_07_knife.png",
            "KNIFE", knifeGlow, false, combatAlpha);
      }
      if (buttons[BTN_RELOAD].visible) {
        String badge = ammoCount > 0 ? String.valueOf(ammoCount) : "0";
        drawCircleActionButton(canvas, buttons[BTN_RELOAD], "actbtn_02_ok_check.png",
            "RLD " + badge, true, false, combatAlpha);
      }
      if (buttons[BTN_ASHLEY].visible) {
        drawCircleActionButton(canvas, buttons[BTN_ASHLEY], "actbtn_15_ashley_cmd.png",
            "ASHLEY", buttons[BTN_ASHLEY].active, false, combatAlpha);
      }
      if (buttons[BTN_SCOPE_IN].visible) {
        drawCircleActionButton(canvas, buttons[BTN_SCOPE_IN], "actbtn_10_scope_2x.png",
            "+", buttons[BTN_SCOPE_IN].active, false, combatAlpha);
      }
      if (buttons[BTN_SCOPE_OUT].visible) {
        drawCircleActionButton(canvas, buttons[BTN_SCOPE_OUT], "actbtn_09_scope_1x.png",
            "-", buttons[BTN_SCOPE_OUT].active, false, combatAlpha);
      }
      String[] qsLabels = {"1", "2", "3", "4"};
      for (int i = 0; i < 4; i++) {
        PadButton q = buttons[BTN_QS_LEFT + i];
        if (q.visible) {
          drawPillButton(canvas, q, qsLabels[i], null, false, combatAlpha * 0.65f);
        }
      }
    }

    // Contextual Event / QTE button draws even during cutscenes!
    if (buttons[BTN_EVENT].visible) {
      String iconName = resolveEventIcon();
      String label = resolveEventLabel();
      drawEventActionButton(canvas, buttons[BTN_EVENT], iconName, label, now);
      if (isQte) postInvalidateOnAnimation();
    }

    // Minimal translucent navigation for menus / typewriter / map / attache case
    String[] navLabels = {"◄", "▲", "►", "▼"};
    for (int i = 0; i < 4; i++) {
      PadButton nb = buttons[BTN_NAV_LEFT + i];
      if (nb.visible) drawPillButton(canvas, nb, navLabels[i], null, false, nb.active ? 0.90f : 0.42f);
    }
    if (buttons[BTN_CONFIRM].visible) {
      drawPillButton(canvas, buttons[BTN_CONFIRM], "OK", null, false,
          buttons[BTN_CONFIRM].active ? 0.95f : 0.55f);
    }
    if (buttons[BTN_CANCEL].visible) {
      drawPillButton(canvas, buttons[BTN_CANCEL], "BACK", null, false,
          buttons[BTN_CANCEL].active ? 0.95f : 0.55f);
    }
    if (buttons[BTN_SUB_L].visible) {
      drawPillButton(canvas, buttons[BTN_SUB_L], "↺ L", null, false,
          buttons[BTN_SUB_L].active ? 0.95f : 0.55f);
    }
    if (buttons[BTN_SUB_R].visible) {
      drawPillButton(canvas, buttons[BTN_SUB_R], "R ↻", null, false,
          buttons[BTN_SUB_R].active ? 0.95f : 0.55f);
    }
  }

  private void drawFloatingStick(Canvas canvas, float alphaScale) {
    boolean touched = stickPid >= 0;
    float baseAlpha = (touched ? 0.78f : 0.28f) * alphaScale;
    int aFill = clampByte(baseAlpha * 140f);
    int aRing = clampByte((touched ? 0.90f : 0.45f) * alphaScale * 255f);

    fillPaint.setColor((aFill << 24) | 0x161412);
    canvas.drawCircle(stickOriginX, stickOriginY, stickRadius, fillPaint);

    ringPaint.setStrokeWidth(touched ? 2.4f * density : 1.5f * density);
    ringPaint.setColor((aRing << 24) | (sprintActive ? 0xF5B841 : 0xC49A5A));
    canvas.drawCircle(stickOriginX, stickOriginY, stickRadius, ringPaint);

    // Subtle sprint outer arc indicator at top of stick ring
    if (touched || !isAimingGun) {
      int aSprint = clampByte((sprintActive ? 1f : 0.35f) * alphaScale * 255f);
      ringPaint.setStrokeWidth(sprintActive ? 3.5f * density : 1.8f * density);
      ringPaint.setColor((aSprint << 24) | (sprintActive ? 0xFFD262 : 0xA88A5B));
      tmpRect.set(stickOriginX - stickRadius * 0.92f, stickOriginY - stickRadius * 0.92f,
          stickOriginX + stickRadius * 0.92f, stickOriginY + stickRadius * 0.92f);
      canvas.drawArc(tmpRect, -125f, 70f, false, ringPaint);
      if (sprintActive) {
        Bitmap sprintBmp = icons.get("actbtn_08_run_sprint.png");
        if (sprintBmp != null) {
          float ir = 12f * density;
          iconPaint.setAlpha(aSprint);
          iconRect.set(stickOriginX - ir, stickOriginY - stickRadius - ir * 1.4f,
              stickOriginX + ir, stickOriginY - stickRadius + ir * 0.6f);
          canvas.drawBitmap(sprintBmp, null, iconRect, iconPaint);
          iconPaint.setAlpha(255);
        }
      }
    }

    // Thumb knob
    float knobR = stickRadius * 0.38f;
    Bitmap knob = icons.get("vpad_knob_34x34.png");
    if (knob != null) {
      iconPaint.setAlpha(clampByte((touched ? 0.95f : 0.55f) * alphaScale * 255f));
      iconRect.set(stickKnobX - knobR, stickKnobY - knobR, stickKnobX + knobR, stickKnobY + knobR);
      canvas.drawBitmap(knob, null, iconRect, iconPaint);
      iconPaint.setAlpha(255);
    } else {
      fillPaint.setColor((aRing << 24) | 0xE8D2A4);
      canvas.drawCircle(stickKnobX, stickKnobY, knobR * 0.7f, fillPaint);
    }
  }

  private void drawCircleActionButton(Canvas canvas, PadButton b, String iconName, String label,
      boolean highlighted, boolean dangerAccent, float alphaScale) {
    float aScale = (b.active ? 0.98f : (highlighted ? 0.88f : 0.70f)) * alphaScale;
    int bgA = clampByte(aScale * 175f);
    int ringA = clampByte(aScale * 255f);

    int fillRgb = b.active ? (dangerAccent ? 0x521C19 : 0x3D3220) : 0x1D1B18;
    int ringRgb = dangerAccent
        ? (highlighted || b.active ? 0xF0685A : 0xC46552)
        : (highlighted || b.active ? 0xF5B841 : 0xB89358);

    fillPaint.setColor((bgA << 24) | fillRgb);
    canvas.drawCircle(b.cx, b.cy, b.r, fillPaint);

    ringPaint.setStrokeWidth((highlighted || b.active ? 2.6f : 1.6f) * density);
    ringPaint.setColor((ringA << 24) | ringRgb);
    canvas.drawCircle(b.cx, b.cy, b.r, ringPaint);

    Bitmap bmp = icons.get(iconName);
    if (bmp != null) {
      float ir = b.r * 0.60f;
      iconPaint.setAlpha(ringA);
      iconRect.set(b.cx - ir, b.cy - ir * 1.08f, b.cx + ir, b.cy + ir * 0.92f);
      canvas.drawBitmap(bmp, null, iconRect, iconPaint);
      iconPaint.setAlpha(255);
    }
    if (label != null && !label.isEmpty()) {
      subTextPaint.setTextSize(Math.max(8.5f * density, b.r * 0.25f));
      subTextPaint.setColor((ringA << 24) | 0xF4E7CA);
      canvas.drawText(label, b.cx, b.cy + b.r * 0.80f, subTextPaint);
    }
  }

  private void drawEventActionButton(Canvas canvas, PadButton b, String iconName, String label, long nowMs) {
    float pulse = isQte ? 0.5f + 0.5f * (float) Math.sin(nowMs * 0.014) : 0f;
    float drawR = b.r * (1f + 0.08f * pulse);

    if (isQte) {
      int haloA = clampByte(110f + 120f * pulse);
      glowPaint.setStrokeWidth((3.5f + 2f * pulse) * density);
      glowPaint.setColor((haloA << 24) | 0xFFD452);
      canvas.drawCircle(b.cx, b.cy, drawR + 5f * density, glowPaint);
    }

    int bgA = b.active ? 225 : 195;
    fillPaint.setColor((bgA << 24) | (isQte ? 0x3A2510 : 0x282117));
    canvas.drawCircle(b.cx, b.cy, drawR, fillPaint);

    ringPaint.setStrokeWidth(2.8f * density);
    ringPaint.setColor(isQte ? 0xFFFFD452 : 0xFFF5B841);
    canvas.drawCircle(b.cx, b.cy, drawR, ringPaint);

    Bitmap bmp = icons.get(iconName);
    if (bmp != null) {
      float ir = drawR * 0.62f;
      iconPaint.setAlpha(255);
      iconRect.set(b.cx - ir, b.cy - ir * 1.08f, b.cx + ir, b.cy + ir * 0.92f);
      canvas.drawBitmap(bmp, null, iconRect, iconPaint);
    }
    if (label != null) {
      subTextPaint.setTextSize(Math.max(9.5f * density, drawR * 0.27f));
      subTextPaint.setColor(0xFFFFF1D0);
      canvas.drawText(label, b.cx, b.cy + drawR * 0.80f, subTextPaint);
    }
  }

  private void drawPillButton(Canvas canvas, PadButton b, String label, String iconName,
      boolean highlighted, float alphaScale) {
    float a = clampFloat(alphaScale, 0f, 1f);
    int bgA = clampByte(a * (b.active ? 220f : 155f));
    int ringA = clampByte(a * 235f);
    float rad = b.halfH;

    tmpRect.set(b.cx - b.halfW, b.cy - b.halfH, b.cx + b.halfW, b.cy + b.halfH);
    fillPaint.setColor((bgA << 24) | (b.active ? 0x3D3220 : 0x1A1816));
    canvas.drawRoundRect(tmpRect, rad, rad, fillPaint);

    ringPaint.setStrokeWidth((b.active || highlighted ? 2f : 1.3f) * density);
    ringPaint.setColor((ringA << 24) | (b.active || highlighted ? 0xF5B841 : 0xB89358));
    canvas.drawRoundRect(tmpRect, rad, rad, ringPaint);

    textPaint.setTextSize(Math.max(9.5f * density, b.halfH * 0.62f));
    textPaint.setColor((ringA << 24) | 0xF3E6CA);
    textPaint.getFontMetrics(tmpFm);
    float textY = b.cy - (tmpFm.ascent + tmpFm.descent) * 0.5f;
    canvas.drawText(label, b.cx, textY, textPaint);
  }

  private String resolveEventIcon() {
    if (actDisp == DISP_L_R || actDisp == DISP_A_B) return "actbtn_32_dodge_lr.png";
    if (actDisp == DISP_A_RAPID || actDisp == DISP_B_RAPID || actDisp == DISP_GACHA
        || actDisp == DISP_A_ACCENT) return "actbtn_23_qte_mash.png";
    if (actDisp == DISP_ROTATE || actDisp == DISP_STICK_A || actDisp == DISP_STICK_UP) {
      return "actbtn_25_lever_crate.png";
    }
    switch (actKind) {
      case ACT_KICK:
      case ACT_SENPUU:
      case ACT_BACKKICK:
        return "actbtn_27_kick_roundhouse.png";
      case ACT_SUPLEX:
      case ACT_EXECUTE:
      case ACT_PALM_SHOCK:
        return "actbtn_28_melee_suplex.png";
      case ACT_OPEN:
      case ACT_THROUGH:
        return "actbtn_18_door_open.png";
      case ACT_PICK_UP:
        return "actbtn_16_pickup_hand.png";
      case ACT_CLIMB:
      case ACT_GO_UP:
      case ACT_PULL_UP:
        return "actbtn_21_ladder_up.png";
      case ACT_GET_DOWN:
      case ACT_JUMP_DOWN:
      case ACT_SLIDE_DOWN:
        return "actbtn_20_jump_down.png";
      case ACT_JUMP_OUT:
      case ACT_JUMP_IN:
      case ACT_JUMP_OVER:
      case ACT_JUMP:
      case ACT_JUMP_BACK:
      case ACT_JUMP_MOVE:
        return "actbtn_26_jump_gap.png";
      case ACT_CATCH:
        return "actbtn_24_catch_ashley.png";
      case ACT_HIDE:
        return "actbtn_31_hide_bin.png";
      case ACT_TALK:
      case ACT_ANSWER:
        return "actbtn_30_speak_window.png";
      case ACT_PUSH:
      case ACT_ROTATE:
      case ACT_OPERATION:
        return "actbtn_25_lever_crate.png";
      case ACT_CHECK:
      case ACT_LOOK:
      case ACT_PEEP:
      case ACT_SAVE:
      default:
        return "actbtn_17_examine_excl.png";
    }
  }

  private String resolveEventLabel() {
    if (actDisp == DISP_L_R || actDisp == DISP_A_B) return "DODGE";
    if (actDisp == DISP_A_RAPID || actDisp == DISP_B_RAPID || actDisp == DISP_GACHA
        || actDisp == DISP_A_ACCENT) return "TAP!";
    if (isQte) return "ACTION";
    switch (actKind) {
      case ACT_KICK:
      case ACT_SENPUU:
      case ACT_BACKKICK:
        return "KICK";
      case ACT_SUPLEX:
      case ACT_EXECUTE:
      case ACT_PALM_SHOCK:
        return "MELEE";
      case ACT_OPEN:
      case ACT_THROUGH:
        return "OPEN";
      case ACT_PICK_UP:
        return "TAKE";
      case ACT_CLIMB:
      case ACT_GO_UP:
      case ACT_PULL_UP:
        return "CLIMB";
      case ACT_GET_DOWN:
      case ACT_JUMP_DOWN:
      case ACT_SLIDE_DOWN:
        return "DROP";
      case ACT_JUMP_OUT:
      case ACT_JUMP_IN:
      case ACT_JUMP_OVER:
      case ACT_JUMP:
      case ACT_JUMP_BACK:
      case ACT_JUMP_MOVE:
        return "JUMP";
      case ACT_CATCH:
        return "CATCH";
      case ACT_HIDE:
        return "HIDE";
      case ACT_TALK:
      case ACT_ANSWER:
        return "TALK";
      case ACT_PUSH:
      case ACT_ROTATE:
      case ACT_OPERATION:
        return "OPERATE";
      case ACT_SAVE:
        return "SAVE";
      case ACT_CHECK:
      case ACT_LOOK:
      case ACT_PEEP:
      default:
        return "EXAMINE";
    }
  }

  private static float clamp(float v, float lo, float hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  private static float clampFloat(float v, float lo, float hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  private static int clampByte(float v) {
    return Math.max(0, Math.min(255, Math.round(v)));
  }
}
