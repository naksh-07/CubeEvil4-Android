package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.view.KeyEvent;
import org.junit.Test;

/**
 * Verifies Xbox 360 (045e:028e), ShanWan PS3/PC clone (2563:0575 under Generic.kl),
 * direct USB XInput report parsing, Xbox 360 Shooter ActionMap, and native L2+X reload.
 */
public class XboxControllerMapTest {
  @Test public void shanwan2563_0575KeysNormaliseToStandardXboxLabels() {
    assertEquals(CoreInput.KEYCODE_BUTTON_Y, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_A));
    assertEquals(CoreInput.KEYCODE_BUTTON_B, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_B));
    assertEquals(CoreInput.KEYCODE_BUTTON_A, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_C));
    assertEquals(CoreInput.KEYCODE_BUTTON_X, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_X));
    assertEquals(CoreInput.KEYCODE_BUTTON_L1, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_Y));
    assertEquals(CoreInput.KEYCODE_BUTTON_R1, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_Z));
    assertEquals(CoreInput.KEYCODE_BUTTON_L2, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_L1));
    assertEquals(CoreInput.KEYCODE_BUTTON_R2, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_R1));
    assertEquals(CoreInput.KEYCODE_BUTTON_SELECT, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_L2));
    assertEquals(CoreInput.KEYCODE_BUTTON_START, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_R2));
    assertEquals(CoreInput.KEYCODE_BUTTON_THUMBL, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_SELECT));
    assertEquals(CoreInput.KEYCODE_BUTTON_THUMBR, CoreInput.normalizeControllerKey(0x2563, 0x0575, true, CoreInput.KEYCODE_BUTTON_START));

    // Standard Xbox 360 (045e:028e) and devices with a vendor .kl (hasCzNoThumbs = false) stay untouched.
    assertEquals(CoreInput.KEYCODE_BUTTON_A, CoreInput.normalizeControllerKey(0x045e, 0x028e, false, CoreInput.KEYCODE_BUTTON_A));
    assertEquals(CoreInput.KEYCODE_BUTTON_A, CoreInput.normalizeControllerKey(0x2563, 0x0575, false, CoreInput.KEYCODE_BUTTON_A));
  }

  @Test public void xboxPresetMapsBToRunAndRBToMap() {
    CoreInput input = new CoreInput();
    input.setControllerMap(ActionMap.xbox(ActionMap.KIND_CONTROLLER));

    // B holds Run (GC B), NOT Map (GC Z), so sprinting / backing out never opens the Map.
    input.key(1, CoreInput.KEYCODE_BUTTON_B, true);
    int[] padB = input.pad();
    assertTrue((padB[0] & CoreInput.GC_B) != 0);
    assertEquals(0, padB[0] & CoreInput.GC_Z);
    input.key(1, CoreInput.KEYCODE_BUTTON_B, false);

    // RB (R1) opens the Map (GC Z).
    input.key(1, CoreInput.KEYCODE_BUTTON_R1, true);
    int[] padRb = input.pad();
    assertTrue((padRb[0] & CoreInput.GC_Z) != 0);
    input.key(1, CoreInput.KEYCODE_BUTTON_R1, false);
  }

  @Test public void aimPlusXUsesNativeReloadInsteadOfQuickReloadShortcut() {
    CoreInput input = new CoreInput();
    input.setControllerMap(ActionMap.xbox(ActionMap.KIND_CONTROLLER));

    // Tap X without aiming -> triggers app Quick Reload.
    assertFalse(input.isAimHeld());
    assertEquals(CoreInput.APP_RELOAD, input.appFunctionOf(CoreInput.KEYCODE_BUTTON_X, false));

    // Hold LT (Aim) -> X suppresses APP_RELOAD and passes through as GC B (native Aim+B Reload).
    input.motion(1, 0, 0, 0, 0, 1f, 0f, 0, 0);
    assertTrue(input.isAimHeld());
    assertEquals(CoreInput.APP_NONE, input.appFunctionOf(CoreInput.KEYCODE_BUTTON_X, false));
    input.key(1, CoreInput.KEYCODE_BUTTON_X, true);
    int[] padReload = input.pad();
    assertTrue((padReload[0] & CoreInput.GC_R) != 0);
    assertTrue((padReload[0] & CoreInput.GC_B) != 0);
  }

  @Test public void remakeLayoutKnifeSlashDoesNotTriggerQuickReload() {
    CoreInput input = new CoreInput();
    input.setControllerMap(ActionMap.remake(ActionMap.KIND_CONTROLLER));

    input.motion(1, 0, 0, 0, 0, 1f, 1f, 0, 0);
    int[] padAimFire = input.pad();
    assertTrue((padAimFire[0] & CoreInput.GC_R) != 0);
    assertTrue((padAimFire[0] & CoreInput.GC_A) != 0);
    assertFalse(input.takeReload());

    input.motion(1, 0, 0, 0, 0, 0f, 0f, 0, 0);
    input.pad();
    input.takeReload();

    input.key(1, CoreInput.KEYCODE_BUTTON_L1, true);
    input.motion(1, 0, 0, 0, 0, 0f, 1f, 0, 0);
    int[] padSlash = input.pad();
    assertTrue((padSlash[0] & CoreInput.GC_L) != 0);
    assertTrue((padSlash[0] & CoreInput.GC_A) != 0);
    assertFalse("slashing with L1+R2 must not trigger quick reload", input.takeReload());
  }

  @Test public void usbXInputParses20ByteReportCorrectly() {
    byte[] pkt = new byte[20];
    pkt[0] = 0x00;
    pkt[1] = 0x14;
    // A (0x1000) + RB (0x0200) -> 0x1200 (little-endian: 0x00, 0x12)
    pkt[2] = 0x00;
    pkt[3] = 0x12;
    pkt[4] = (byte) 255; // LT full
    pkt[5] = 0x00;       // RT released
    // LX = +16384 (0x4000), LY = +32767 (0x7FFF -> CoreInput Y is negated ~ -1.0f)
    pkt[6] = 0x00;
    pkt[7] = 0x40;
    pkt[8] = (byte) 0xFF;
    pkt[9] = 0x7F;

    UsbXInputDriver.State st = UsbXInputDriver.parseReport(pkt, pkt.length);
    assertNotNull(st);
    assertEquals(UsbXInputDriver.BTN_A | UsbXInputDriver.BTN_RB, st.buttons);
    assertEquals(1.0f, st.lt, 0.01f);
    assertEquals(0.0f, st.rt, 0.01f);
    assertEquals(0.5f, st.lx, 0.01f);
    assertEquals(-1.0f, st.ly, 0.01f);
    assertEquals(KeyEvent.KEYCODE_BUTTON_A, UsbXInputDriver.BIT_TO_KEYCODE[23]);

    byte[] off = UsbXInputDriver.rumblePacket(0);
    assertEquals(8, off.length);
    assertEquals(0, off[3]);
    assertEquals(0, off[4]);

    byte[] on = UsbXInputDriver.rumblePacket(2);
    assertTrue((on[3] & 0xFF) > 0);
    assertTrue((on[4] & 0xFF) > 0);
  }
}
