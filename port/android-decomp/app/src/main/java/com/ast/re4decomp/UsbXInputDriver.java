package com.ast.re4decomp;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Handler;
import android.util.Log;
import android.view.KeyEvent;
import java.util.Arrays;

/**
 * Direct USB Host XInput reader for wired Xbox 360 controllers (VID 0x045e, PID 0x028e),
 * especially third-party ShanWan clones whose kernel xpad handshake stalls on Android 5.10 kernels
 * (stuck on LED #2 with zero input events until switched manually to HID mode).
 */
final class UsbXInputDriver {
  private static final String TAG = "RE4UsbPad";
  private static final String ACTION_USB_PERMISSION = "com.ast.cubeevil4.USB_XINPUT_PERMISSION";
  static final int USB_DEVICE_ID = -9001;

  static final int BTN_UP = 0x0001, BTN_DOWN = 0x0002, BTN_LEFT = 0x0004, BTN_RIGHT = 0x0008,
      BTN_START = 0x0010, BTN_BACK = 0x0020, BTN_LS = 0x0040, BTN_RS = 0x0080,
      BTN_LB = 0x0100, BTN_RB = 0x0200, BTN_GUIDE = 0x0400,
      BTN_A = 0x1000, BTN_B = 0x2000, BTN_X = 0x4000, BTN_Y = 0x8000;

  /** Pure-Java decoded 20-byte XInput packet for host unit testing. */
  static final class State {
    final int buttons;
    final float lt, rt, lx, ly, rx, ry;
    State(int buttons, float lt, float rt, float lx, float ly, float rx, float ry) {
      this.buttons = buttons;
      this.lt = lt;
      this.rt = rt;
      this.lx = lx;
      this.ly = ly;
      this.rx = rx;
      this.ry = ry;
    }
  }

  /** Parses an Xbox 360 20-byte XInput packet (message type 0x00, length 0x14). Returns null if invalid. */
  static State parseReport(byte[] buf, int len) {
    if (buf == null || len < 14 || buf[0] != 0x00 || (buf[1] & 0xFF) < 14) return null;
    int buttons = (buf[2] & 0xFF) | ((buf[3] & 0xFF) << 8);
    float lt = (buf[4] & 0xFF) / 255f;
    float rt = (buf[5] & 0xFF) / 255f;
    float lx = s16(buf[6], buf[7]) / 32768f;
    float ly = -(s16(buf[8], buf[9]) / 32768f); // Android CoreInput expects Y down-positive
    float rx = s16(buf[10], buf[11]) / 32768f;
    float ry = -(s16(buf[12], buf[13]) / 32768f);
    return new State(buttons, lt, rt, lx, ly, rx, ry);
  }

  private static int s16(byte lo, byte hi) {
    return (short) ((lo & 0xFF) | ((hi & 0xFF) << 8));
  }

  static final int[] BIT_TO_KEYCODE = {
      BTN_UP, KeyEvent.KEYCODE_DPAD_UP,
      BTN_DOWN, KeyEvent.KEYCODE_DPAD_DOWN,
      BTN_LEFT, KeyEvent.KEYCODE_DPAD_LEFT,
      BTN_RIGHT, KeyEvent.KEYCODE_DPAD_RIGHT,
      BTN_START, KeyEvent.KEYCODE_BUTTON_START,
      BTN_BACK, KeyEvent.KEYCODE_BUTTON_SELECT,
      BTN_LS, KeyEvent.KEYCODE_BUTTON_THUMBL,
      BTN_RS, KeyEvent.KEYCODE_BUTTON_THUMBR,
      BTN_LB, KeyEvent.KEYCODE_BUTTON_L1,
      BTN_RB, KeyEvent.KEYCODE_BUTTON_R1,
      BTN_GUIDE, KeyEvent.KEYCODE_BUTTON_MODE,
      BTN_A, KeyEvent.KEYCODE_BUTTON_A,
      BTN_B, KeyEvent.KEYCODE_BUTTON_B,
      BTN_X, KeyEvent.KEYCODE_BUTTON_X,
      BTN_Y, KeyEvent.KEYCODE_BUTTON_Y,
  };

  interface Listener {
    void onUsbButton(int keyCode, boolean down);
    void onUsbMotion(float lx, float ly, float rx, float ry, float lt, float rt, float hatX, float hatY);
    void onUsbStatus(String message, boolean isUserHint);
    void onUsbDisconnected();
  }

  private final Context context;
  private final Handler main;
  private final Listener listener;
  private UsbManager usb;
  private boolean registered;
  private volatile boolean running;
  private UsbDeviceConnection connection;
  private UsbInterface claimedInterface;
  private UsbEndpoint epOut;
  private Thread readerThread;
  private int activeDeviceId = -1;
  private int permissionRequestedId = -1;
  private int lastButtons;
  private int lastRumbleLevel;
  private long lastProbeAt;
  private boolean kernelBlockedNotified;
  private final java.util.concurrent.ExecutorService rumbleExecutor =
      java.util.concurrent.Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "RE4UsbRumble");
        t.setDaemon(true);
        return t;
      });

  private final BroadcastReceiver receiver = new BroadcastReceiver() {
    @Override public void onReceive(Context c, Intent intent) {
      String action = intent.getAction();
      if (ACTION_USB_PERMISSION.equals(action)) {
        UsbDevice dev = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
        boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
        Log.i(TAG, "USB permission " + (granted ? "granted" : "denied")
            + (dev == null ? "" : " for " + hex(dev.getVendorId()) + ":" + hex(dev.getProductId())));
        if (granted && dev != null) startDevice(dev);
      } else if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
        kernelBlockedNotified = false;
        permissionRequestedId = -1;
        probeSoon();
      } else if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
        UsbDevice dev = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
        if (dev != null && dev.getDeviceId() == activeDeviceId) stopStream();
      }
    }
  };

  UsbXInputDriver(Context context, Handler main, Listener listener) {
    this.context = context.getApplicationContext();
    this.main = main;
    this.listener = listener;
  }

  void resume() {
    if (usb == null) usb = (UsbManager) context.getSystemService(Context.USB_SERVICE);
    if (!registered) {
      IntentFilter f = new IntentFilter();
      f.addAction(ACTION_USB_PERMISSION);
      f.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
      f.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
      if (Build.VERSION.SDK_INT >= 33) {
        context.registerReceiver(receiver, f, Context.RECEIVER_NOT_EXPORTED);
      } else {
        context.registerReceiver(receiver, f);
      }
      registered = true;
    }
    probeSoon();
  }

  void pause() {
    stopStream();
    if (registered) {
      try { context.unregisterReceiver(receiver); } catch (IllegalArgumentException ignored) {}
      registered = false;
    }
  }

  boolean isActive() { return running && connection != null; }

  void probeIfKernelSilent(boolean kernel045eSeen, boolean kernel045eSentInput) {
    if (running || usb == null) return;
    UsbDevice target = findXInputDevice();
    if (target == null) return;
    String mfr = target.getManufacturerName();
    boolean shanwan = mfr != null && mfr.toLowerCase(java.util.Locale.ROOT).contains("shanwan");
    long now = System.currentTimeMillis();
    if (!shanwan && (!kernel045eSeen || kernel045eSentInput)) return;
    if (now - lastProbeAt < 2500) return;
    lastProbeAt = now;
    tryOpen(target);
  }

  private void probeSoon() {
    main.postDelayed(() -> {
      UsbDevice dev = findXInputDevice();
      if (dev != null && !running) {
        String mfr = dev.getManufacturerName();
        Log.i(TAG, "USB XInput device visible: " + hex(dev.getVendorId()) + ":" + hex(dev.getProductId())
            + " mfr=" + mfr + " prod=" + dev.getProductName());
        if (mfr != null && mfr.toLowerCase(java.util.Locale.ROOT).contains("shanwan")) {
          tryOpen(dev);
        }
      }
    }, 600);
  }

  private UsbDevice findXInputDevice() {
    if (usb == null) return null;
    for (UsbDevice d : usb.getDeviceList().values()) {
      if (d.getVendorId() == 0x045e && d.getProductId() == 0x028e) return d;
      for (int i = 0; i < d.getInterfaceCount(); i++) {
        UsbInterface intf = d.getInterface(i);
        if (intf.getInterfaceClass() == 255 && intf.getInterfaceSubclass() == 93
            && intf.getInterfaceProtocol() == 1) {
          return d;
        }
      }
    }
    return null;
  }

  private void tryOpen(UsbDevice dev) {
    if (usb == null || running) return;
    if (usb.hasPermission(dev)) {
      startDevice(dev);
    } else if (permissionRequestedId != dev.getDeviceId()) {
      permissionRequestedId = dev.getDeviceId();
      int flags = Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT
          : PendingIntent.FLAG_UPDATE_CURRENT;
      Intent intent = new Intent(ACTION_USB_PERMISSION).setPackage(context.getPackageName());
      usb.requestPermission(dev, PendingIntent.getBroadcast(context, 0, intent, flags));
      Log.i(TAG, "requested USB permission for " + hex(dev.getVendorId()) + ":" + hex(dev.getProductId()));
    }
  }

  private synchronized void startDevice(UsbDevice dev) {
    if (running) return;
    UsbInterface target = null;
    for (int i = 0; i < dev.getInterfaceCount(); i++) {
      UsbInterface intf = dev.getInterface(i);
      if (intf.getInterfaceClass() == 255 && intf.getInterfaceSubclass() == 93
          && intf.getInterfaceProtocol() == 1) {
        target = intf;
        break;
      }
    }
    if (target == null && dev.getInterfaceCount() > 0) target = dev.getInterface(0);
    if (target == null) return;

    UsbEndpoint in = null, out = null;
    for (int i = 0; i < target.getEndpointCount(); i++) {
      UsbEndpoint ep = target.getEndpoint(i);
      if (ep.getType() == UsbConstants.USB_ENDPOINT_XFER_INT) {
        if (ep.getDirection() == UsbConstants.USB_DIR_IN && in == null) in = ep;
        else if (ep.getDirection() == UsbConstants.USB_DIR_OUT && out == null) out = ep;
      }
    }
    if (in == null) return;

    UsbDeviceConnection conn = usb.openDevice(dev);
    if (conn == null) {
      Log.w(TAG, "openDevice failed");
      return;
    }
    boolean claimed = false;
    try {
      claimed = conn.claimInterface(target, true);
    } catch (RuntimeException e) {
      Log.w(TAG, "claimInterface threw: " + e);
    }
    if (!claimed) {
      conn.close();
      Log.w(TAG, "claimInterface(force=true) denied by Android kernel/SELinux for 045e:028e");
      if (!kernelBlockedNotified) {
        kernelBlockedNotified = true;
        main.post(() -> listener.onUsbStatus(
            "Controller stalled in phone kernel (045e:028e LED 2) — hold Home 3s to switch to LED 1 mode", true));
      }
      return;
    }

    // Wake packet for ShanWan / clone 045e:028e controllers + Player 1 solid LED (0x01 0x03 0x06).
    byte[] dummy = new byte[20];
    int ctrl = conn.controlTransfer(0xC1, 0x01, 0x0100, 0x0000, dummy, dummy.length, 150);
    Log.i(TAG, "USB XInput claimed iface " + target.getId() + ", wake ctrl=" + ctrl);
    if (out != null) {
      byte[] led1 = new byte[] {0x01, 0x03, 0x06};
      conn.bulkTransfer(out, led1, led1.length, 100);
      conn.bulkTransfer(out, rumblePacket(0), 8, 50);
    }

    connection = conn;
    claimedInterface = target;
    epOut = out;
    activeDeviceId = dev.getDeviceId();
    lastButtons = 0;
    lastRumbleLevel = 0;
    running = true;
    final UsbEndpoint epIn = in;
    main.post(() -> listener.onUsbStatus("Xbox 360 Direct USB active (LED 1)", false));

    readerThread = new Thread(() -> readLoop(conn, epIn), "RE4UsbXInput");
    readerThread.start();
  }

  private void readLoop(UsbDeviceConnection conn, UsbEndpoint epIn) {
    byte[] buf = new byte[Math.max(32, epIn.getMaxPacketSize())];
    byte[] prev = new byte[14];
    boolean havePrev = false;
    while (running) {
      int n = conn.bulkTransfer(epIn, buf, buf.length, 50);
      if (n <= 0) continue;
      final State st = parseReport(buf, n);
      if (st == null) continue;
      if (havePrev && Arrays.equals(Arrays.copyOf(buf, 14), prev)) continue;
      System.arraycopy(buf, 0, prev, 0, 14);
      havePrev = true;
      main.post(() -> dispatchState(st));
    }
  }

  private void dispatchState(State st) {
    if (!running) return;
    int diff = st.buttons ^ lastButtons;
    if (diff != 0) {
      for (int i = 0; i < BIT_TO_KEYCODE.length; i += 2) {
        int mask = BIT_TO_KEYCODE[i];
        if ((diff & mask) != 0) {
          listener.onUsbButton(BIT_TO_KEYCODE[i + 1], (st.buttons & mask) != 0);
        }
      }
      lastButtons = st.buttons;
    }
    listener.onUsbMotion(st.lx, st.ly, st.rx, st.ry, st.lt, st.rt, 0f, 0f);
  }

  static byte[] rumblePacket(int level) {
    int clamped = level <= 0 ? 0 : Math.min(4, level);
    int big = clamped == 0 ? 0 : Math.min(255, 80 + clamped * 42);
    int small = clamped == 0 ? 0 : Math.min(255, 60 + clamped * 48);
    return new byte[] {0x00, 0x08, 0x00, (byte) big, (byte) small, 0x00, 0x00, 0x00};
  }

  synchronized void setRumble(int level) {
    int norm = level <= 0 ? 0 : Math.min(4, level);
    UsbDeviceConnection conn = connection;
    UsbEndpoint out = epOut;
    if (conn == null || out == null || (!running && norm != 0)) {
      lastRumbleLevel = 0;
      return;
    }
    if (norm == lastRumbleLevel) return;
    lastRumbleLevel = norm;
    byte[] pkt = rumblePacket(norm);
    rumbleExecutor.execute(() -> {
      try {
        conn.bulkTransfer(out, pkt, pkt.length, 20);
      } catch (RuntimeException ignored) {}
    });
  }

  synchronized void stopStream() {
    if (!running && connection == null) return;
    running = false;
    lastRumbleLevel = 0;
    Thread t = readerThread;
    readerThread = null;
    UsbDeviceConnection conn = connection;
    UsbInterface intf = claimedInterface;
    UsbEndpoint out = epOut;
    connection = null;
    claimedInterface = null;
    epOut = null;
    activeDeviceId = -1;
    if (conn != null) {
      if (out != null) {
        try { conn.bulkTransfer(out, rumblePacket(0), 8, 20); } catch (RuntimeException ignored) {}
      }
      try { if (intf != null) conn.releaseInterface(intf); } catch (RuntimeException ignored) {}
      try { conn.close(); } catch (RuntimeException ignored) {}
    }
    if (t != null && t != Thread.currentThread()) {
      try { t.join(120); } catch (InterruptedException ignored) {}
    }
    if (lastButtons != 0) {
      int prev = lastButtons;
      lastButtons = 0;
      main.post(() -> {
        for (int i = 0; i < BIT_TO_KEYCODE.length; i += 2) {
          if ((prev & BIT_TO_KEYCODE[i]) != 0) listener.onUsbButton(BIT_TO_KEYCODE[i + 1], false);
        }
        listener.onUsbDisconnected();
      });
    } else {
      main.post(listener::onUsbDisconnected);
    }
  }

  private static String hex(int v) {
    return String.format(java.util.Locale.ROOT, "%04x", v & 0xFFFF);
  }
}
