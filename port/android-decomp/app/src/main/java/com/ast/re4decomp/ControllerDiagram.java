package com.ast.re4decomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.KeyEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;
import java.util.function.IntFunction;

/**
 * The controller's mapping as a picture (wish of DevJunior, 18.11.2026: "the Xbox controller with its buttons and
 * their legend", for the remake's layout): a drawn pad of the Xbox kind (no maker's marks), each button with a line to
 * its legend, the actions the current mapping puts on it (ActionMap.legend). The menu's Button Mapping page shows it
 * above its list, for any mapping of the controller.
 */
final class ControllerDiagram extends View {
  // the menu's colours (CoreMenu)
  private static final int INK = 0xfff0eee5, MUTED = 0xffa6afa3, GOLD = 0xffd7bc7d;
  private static final int BODY = 0xff2b2d2a, EDGE = 0xff6d7268, PART = 0xff3d403b;
  private static final int XA = 0xff5fb04a, XB = 0xffd2483d, XX = 0xff3f7fd0, XY = 0xffe2b93b;

  /**
   * A button: its key, its centre in the pad's units (0..1 across, 0..1 down; the triggers above 0), its side, its
   * radius (its line ends at its edge), whether its line goes across first and then down (the two small buttons
   * between the sticks: a straight one would cross a stick or the face buttons).
   */
  private static final class Spot {
    final int key; final float u, v, r; final boolean right, elbow; final String name;
    Spot(int key, float u, float v, boolean right, String name, float r, boolean elbow) {
      this.key = key; this.u = u; this.v = v; this.right = right; this.name = name; this.r = r; this.elbow = elbow;
    }
  }

  // the legends' order, top to bottom on each side
  private static final Spot[] SPOTS = {
      new Spot(KeyEvent.KEYCODE_BUTTON_L2, 0.25f, -0.09f, false, "LT", 0f, false),
      new Spot(KeyEvent.KEYCODE_BUTTON_SELECT, 0.43f, 0.30f, false, "View", 0.022f, true),
      new Spot(KeyEvent.KEYCODE_BUTTON_L1, 0.25f, 0.035f, false, "LB", 0f, false),
      new Spot(KeyEvent.KEYCODE_BUTTON_THUMBL, 0.27f, 0.30f, false, "LS", 0.085f, false),
      new Spot(KeyEvent.KEYCODE_BUTTON_R2, 0.75f, -0.09f, true, "RT", 0f, false),
      new Spot(KeyEvent.KEYCODE_BUTTON_START, 0.57f, 0.30f, true, "Menu", 0.022f, true),
      new Spot(KeyEvent.KEYCODE_BUTTON_R1, 0.75f, 0.035f, true, "RB", 0f, false),
      new Spot(KeyEvent.KEYCODE_BUTTON_Y, 0.75f, 0.19f, true, "Y", 0.04f, false),
      new Spot(KeyEvent.KEYCODE_BUTTON_B, 0.83f, 0.30f, true, "B", 0.04f, false),
      new Spot(KeyEvent.KEYCODE_BUTTON_A, 0.75f, 0.41f, true, "A", 0.04f, false),
      new Spot(KeyEvent.KEYCODE_BUTTON_X, 0.67f, 0.30f, true, "X", 0.04f, false),
      new Spot(KeyEvent.KEYCODE_BUTTON_THUMBR, 0.62f, 0.52f, true, "RS", 0.085f, false),
  };

  /** The Xbox name of a controller key (the legends' combinations), else the menu's. */
  static String xboxName(int keyCode) {
    for (Spot s : SPOTS) if (s.key == keyCode) return s.name;
    return CoreMenu.keyName(keyCode);
  }

  private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), line = new Paint(Paint.ANTI_ALIAS_FLAG),
      text = new Paint(Paint.ANTI_ALIAS_FLAG), head = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final List<List<String>> lines = new ArrayList<>();
  private final float density;

  /**
   * `label` names an action; `move`, `camera`: the sticks' own legends; `press`: "%s (press)" for a stick's click.
   */
  ControllerDiagram(Context c, ActionMap m, IntFunction<String> label, String move, String camera, String press) {
    super(c);
    density = c.getResources().getDisplayMetrics().density;
    for (Spot s : SPOTS) {
      List<String> l = new ArrayList<>();
      if (s.key == KeyEvent.KEYCODE_BUTTON_THUMBL) l.add(move);
      if (s.key == KeyEvent.KEYCODE_BUTTON_THUMBR) l.add(camera);
      boolean stick = s.key == KeyEvent.KEYCODE_BUTTON_THUMBL || s.key == KeyEvent.KEYCODE_BUTTON_THUMBR;
      for (String e : m.legend(s.key, label, ControllerDiagram::xboxName)) l.add(stick ? String.format(press, e) : e);
      lines.add(l);
    }
    line.setStyle(Paint.Style.STROKE);
    head.setTypeface(Typeface.DEFAULT_BOLD);
    setFocusable(false);
    setContentDescription(describe());
  }

  /** For talkback: "A: Action. RB: Run. …" */
  private String describe() {
    StringBuilder b = new StringBuilder();
    for (int i = 0; i < SPOTS.length; i++) {
      if (lines.get(i).isEmpty()) continue;
      b.append(SPOTS[i].name).append(": ").append(String.join(", ", lines.get(i))).append(". ");
    }
    return b.toString();
  }

  @Override protected void onMeasure(int wSpec, int hSpec) {
    int w = MeasureSpec.getSize(wSpec);
    setMeasuredDimension(w, Math.round(Math.min(w * 0.5f, 270 * density)));
  }

  @Override protected void onDraw(Canvas canvas) {
    float w = getWidth(), h = getHeight();
    float cw = Math.min(w * 0.36f, h * 1.15f), ch = cw * 0.7f;
    float x0 = (w - cw) / 2, y0 = h * 0.24f;
    float colW = (w - cw) / 2 - 22 * density;
    // the legends' size: 12 sp, smaller (down to 8) when a line is wider than its column or the lines too many
    float size = 12 * density;
    float most = 0;
    int rowsL = 0, rowsR = 0;
    text.setTextSize(size);
    head.setTextSize(size);
    for (int i = 0; i < SPOTS.length; i++) {
      List<String> l = lines.get(i);
      if (l.isEmpty()) continue;
      if (SPOTS[i].right) rowsR += l.size(); else rowsL += l.size();
      for (int j = 0; j < l.size(); j++) {
        most = Math.max(most, head.measureText(SPOTS[i].name + "  ") + text.measureText(l.get(j)));
      }
    }
    int rows = Math.max(rowsL, rowsR) + 4;
    float scale = Math.min(1f, Math.min(colW / Math.max(1f, most), h / (rows * size * 1.25f)));
    size *= Math.max(scale, 8f / 12f);
    text.setTextSize(size);
    head.setTextSize(size);
    text.setColor(INK);
    head.setColor(GOLD);
    drawPad(canvas, x0, y0, cw, ch);
    // the legends' lines as shown: a line too long for its column wraps at its words (the rest ellipsized)
    List<List<String>> shown = new ArrayList<>();
    rowsL = 0;
    rowsR = 0;
    for (int i = 0; i < SPOTS.length; i++) {
      float room = colW - head.measureText(SPOTS[i].name + "  ");
      List<String> l = new ArrayList<>();
      for (String e : lines.get(i)) l.addAll(wrap(e, room));
      shown.add(l);
      if (SPOTS[i].right) rowsR += l.size(); else rowsL += l.size();
    }
    // the legends from the body's top down, a line to each button
    line.setStrokeWidth(1 * density);
    line.setColor(MUTED);
    float across = y0 + 0.11f * ch;  // the small buttons' lines: across between the bumpers and the sticks
    for (int side = 0; side < 2; side++) {
      boolean right = side == 1;
      int n = right ? rowsR : rowsL;
      int blocks = 0;
      for (int i = 0; i < SPOTS.length; i++) if (SPOTS[i].right == right && !shown.get(i).isEmpty()) blocks++;
      float step = size * 1.25f, gap = size * 0.6f;
      float total = n * step + Math.max(0, blocks - 1) * gap;
      float y = Math.max(size, Math.min(y0 + size * 0.8f, h - total));
      float tx = right ? x0 + cw + 18 * density : x0 - 18 * density;
      float edge = right ? x0 + 0.99f * cw : x0 + 0.01f * cw;
      for (int i = 0; i < SPOTS.length; i++) {
        Spot s = SPOTS[i];
        List<String> l = shown.get(i);
        if (s.right != right || l.isEmpty()) continue;
        float bx = x0 + s.u * cw, by = y0 + s.v * ch;
        float ly = y - size * 0.35f;
        float lx = right ? tx - 4 * density : tx + 4 * density;
        float rr = s.r * cw;
        if (s.elbow) {
          // to the body's edge, across above the face buttons / the stick, down onto the button
          canvas.drawLine(lx, ly, edge, across, line);
          canvas.drawLine(edge, across, bx, across, line);
          canvas.drawLine(bx, across, bx, by - rr, line);
          canvas.drawCircle(bx, by - rr, 2.2f * density, line);
        } else {
          float len = (float) Math.hypot(bx - lx, by - ly);
          float ex = bx - (bx - lx) / Math.max(1f, len) * rr, ey = by - (by - ly) / Math.max(1f, len) * rr;
          canvas.drawLine(lx, ly, ex, ey, line);
          canvas.drawCircle(ex, ey, 2.2f * density, line);
        }
        String name = s.name + "  ";
        float nameW = head.measureText(name);
        for (int j = 0; j < l.size(); j++) {
          String e = l.get(j);
          if (right) {
            if (j == 0) canvas.drawText(name, tx, y, head);
            canvas.drawText(e, tx + nameW, y, text);
          } else {
            float ew = text.measureText(e);
            if (j == 0) canvas.drawText(name, tx - ew - nameW, y, head);
            canvas.drawText(e, tx - ew, y, text);
          }
          y += step;
        }
        y += gap;
      }
    }
  }

  /** `s` in lines of at most `room` wide, broken at spaces; at most two, the second ellipsized. */
  private List<String> wrap(String s, float room) {
    List<String> out = new ArrayList<>();
    if (text.measureText(s) <= room) {
      out.add(s);
      return out;
    }
    int cut = -1;
    for (int i = s.indexOf(' '); i > 0; i = s.indexOf(' ', i + 1)) {
      if (text.measureText(s, 0, i) <= room) cut = i; else break;
    }
    if (cut <= 0) {
      out.add(ellipsize(s, room));
      return out;
    }
    out.add(s.substring(0, cut));
    out.add(ellipsize(s.substring(cut + 1), room));
    return out;
  }

  private String ellipsize(String s, float room) {
    if (text.measureText(s) <= room || s.length() < 2) return s;
    String e = s;
    while (e.length() > 1 && text.measureText(e + "\u2026") > room) e = e.substring(0, e.length() - 1);
    return e + "\u2026";
  }

  /** The pad: body, triggers, bumpers, sticks, D-pad, the face buttons and the two small ones. */
  private void drawPad(Canvas c, float x0, float y0, float cw, float ch) {
    float d = density;
    // triggers and bumpers behind the body
    fill.setColor(PART);
    line.setStrokeWidth(1.2f * d);
    line.setColor(EDGE);
    for (float u : new float[] {0.25f, 0.75f}) {
      RectF t = new RectF(x0 + (u - 0.07f) * cw, y0 - 0.17f * ch, x0 + (u + 0.07f) * cw, y0 - 0.01f * ch);
      c.drawRoundRect(t, 6 * d, 6 * d, fill);
      c.drawRoundRect(t, 6 * d, 6 * d, line);
      RectF b = new RectF(x0 + (u - 0.12f) * cw, y0 + 0.0f * ch, x0 + (u + 0.12f) * cw, y0 + 0.07f * ch);
      c.drawRoundRect(b, 5 * d, 5 * d, fill);
      c.drawRoundRect(b, 5 * d, 5 * d, line);
    }
    // the body: shoulders, two grips, the inner arch
    Path p = new Path();
    p.moveTo(x0 + 0.20f * cw, y0 + 0.05f * ch);
    p.cubicTo(x0 + 0.35f * cw, y0 + 0.02f * ch, x0 + 0.65f * cw, y0 + 0.02f * ch, x0 + 0.80f * cw, y0 + 0.05f * ch);
    p.cubicTo(x0 + 0.95f * cw, y0 + 0.08f * ch, x0 + 0.99f * cw, y0 + 0.30f * ch, x0 + 1.0f * cw, y0 + 0.55f * ch);
    p.cubicTo(x0 + 1.01f * cw, y0 + 0.85f * ch, x0 + 0.95f * cw, y0 + 1.0f * ch, x0 + 0.86f * cw, y0 + 0.98f * ch);
    p.cubicTo(x0 + 0.78f * cw, y0 + 0.96f * ch, x0 + 0.74f * cw, y0 + 0.72f * ch, x0 + 0.66f * cw, y0 + 0.68f * ch);
    p.cubicTo(x0 + 0.58f * cw, y0 + 0.65f * ch, x0 + 0.42f * cw, y0 + 0.65f * ch, x0 + 0.34f * cw, y0 + 0.68f * ch);
    p.cubicTo(x0 + 0.26f * cw, y0 + 0.72f * ch, x0 + 0.22f * cw, y0 + 0.96f * ch, x0 + 0.14f * cw, y0 + 0.98f * ch);
    p.cubicTo(x0 + 0.05f * cw, y0 + 1.0f * ch, x0 - 0.01f * cw, y0 + 0.85f * ch, x0 + 0.0f * cw, y0 + 0.55f * ch);
    p.cubicTo(x0 + 0.01f * cw, y0 + 0.30f * ch, x0 + 0.05f * cw, y0 + 0.08f * ch, x0 + 0.20f * cw, y0 + 0.05f * ch);
    p.close();
    fill.setColor(BODY);
    c.drawPath(p, fill);
    line.setStrokeWidth(1.5f * d);
    c.drawPath(p, line);
    // the round middle button (no mark) and the two small ones
    fill.setColor(PART);
    c.drawCircle(x0 + 0.5f * cw, y0 + 0.17f * ch, 0.045f * cw, fill);
    c.drawCircle(x0 + 0.5f * cw, y0 + 0.17f * ch, 0.045f * cw, line);
    for (float u : new float[] {0.43f, 0.57f}) {
      c.drawCircle(x0 + u * cw, y0 + 0.30f * ch, 0.022f * cw, fill);
      c.drawCircle(x0 + u * cw, y0 + 0.30f * ch, 0.022f * cw, line);
    }
    // sticks
    for (float[] s : new float[][] {{0.27f, 0.30f}, {0.62f, 0.52f}}) {
      fill.setColor(PART);
      c.drawCircle(x0 + s[0] * cw, y0 + s[1] * ch, 0.085f * cw, fill);
      c.drawCircle(x0 + s[0] * cw, y0 + s[1] * ch, 0.085f * cw, line);
      fill.setColor(0xff1e201d);
      c.drawCircle(x0 + s[0] * cw, y0 + s[1] * ch, 0.058f * cw, fill);
    }
    // D-pad
    float dx = x0 + 0.38f * cw, dy = y0 + 0.52f * ch, a = 0.055f * cw, t = 0.02f * cw;
    fill.setColor(PART);
    c.drawRect(dx - a, dy - t, dx + a, dy + t, fill);
    c.drawRect(dx - t, dy - a, dx + t, dy + a, fill);
    // face buttons in their colours
    float r = 0.04f * cw;
    int[] col = {XY, XB, XX, XA};
    float[][] at = {{0.75f, 0.19f}, {0.83f, 0.30f}, {0.67f, 0.30f}, {0.75f, 0.41f}};
    String[] letter = {"Y", "B", "X", "A"};
    Paint l = new Paint(Paint.ANTI_ALIAS_FLAG);
    l.setTypeface(Typeface.DEFAULT_BOLD);
    l.setTextAlign(Paint.Align.CENTER);
    l.setTextSize(r * 1.2f);
    l.setColor(0xff151614);
    for (int i = 0; i < 4; i++) {
      fill.setColor(col[i]);
      c.drawCircle(x0 + at[i][0] * cw, y0 + at[i][1] * ch, r, fill);
      c.drawText(letter[i], x0 + at[i][0] * cw, y0 + at[i][1] * ch + r * 0.42f, l);
    }
    // the triggers' and bumpers' names on them
    l.setColor(MUTED);
    l.setTextSize(0.045f * cw);
    c.drawText("LT", x0 + 0.25f * cw, y0 - 0.06f * ch, l);
    c.drawText("RT", x0 + 0.75f * cw, y0 - 0.06f * ch, l);
  }
}
