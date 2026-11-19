package com.ast.re4decomp;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.graphics.drawable.GradientDrawable;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * CubeEvil4 Studio (Mods, 19.11.2026; port/docs/studio-plan.md). DevJunior's choice: an app screen of its own, without
 * the game running underneath; the viewer and the animations first. Left: the tabs (Models, Animations, Convert, Edit)
 * and their lists; right: the 3D view (cpp/studio_gl.cpp) with the floor grid on X/Z and the axes (X red, Y green,
 * Z blue). One finger turns the camera, two fingers move it, a pinch zooms. The models come straight from the
 * imported discs (game/disc1, game/disc2: files/em/*.drs) and from the mod packs (game/mods).
 */
public final class StudioActivity extends Activity {
  private static final String TAG = "RE4Studio";
  private static final int BG = 0xFF101216, PANEL = 0xFF171A20, CARD = 0xFF20242C, ACCENT = 0xFFE0A23A,
      TEXT = 0xFFE8EAED, DIM = 0xFF9AA0A8;

  private final Handler main = new Handler(Looper.getMainLooper());
  private LinearLayout content;
  private final List<Button> tabs = new ArrayList<>();
  private int tab;
  private TextView title, info, frameLabel;
  private SeekBar seek;
  private Button play;
  private GLSurfaceView view;

  // the view's state (the GL thread reads it)
  private volatile float yaw = 0.6f, pitch = 0.25f, distance = 3500f, tx, ty = 900f, tz;
  private volatile int flags = 1 | 2 | 8;  // grid, axes, textures
  private volatile boolean playing = true;
  private volatile float speed = 1f, frame, length;
  private long lastNanos;
  private boolean seeking;

  private StudioFiles files;
  private String archivePath;      // the archive whose models are listed, below game/ (null: the archives)
  private String archiveName = "";
  private int highlighted = -1;    // the piece tinted in the view (its place in StudioBridge.parts())
  private String source = "disc1"; // disc1, disc2, mods
  private String kind = "all";     // all, pl, em, wep, other
  private boolean modelLoaded;

  @Override protected void onCreate(Bundle state) {
    super.onCreate(state);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    StudioBridge.load();
    files = StudioFiles.open(this);
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.HORIZONTAL);
    root.setBackgroundColor(BG);

    // ---- the left panel: title, tabs, lists
    LinearLayout panel = new LinearLayout(this);
    panel.setOrientation(LinearLayout.VERTICAL);
    panel.setBackgroundColor(PANEL);
    panel.setPadding(dp(14), dp(12), dp(14), dp(8));
    root.addView(panel, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 0.36f));
    TextView name = text("CubeEvil4 Studio", 20, TEXT, true);
    panel.addView(name);
    TextView sub = text("Models and animations of your discs and mods", 12, DIM, false);
    panel.addView(sub);
    LinearLayout tabRow = new LinearLayout(this);
    tabRow.setOrientation(LinearLayout.HORIZONTAL);
    tabRow.setPadding(0, dp(10), 0, dp(8));
    String[] names = {"Models", "Animations", "Convert", "Edit"};
    for (int i = 0; i < names.length; i++) {
      final int index = i;
      Button b = pill(names[i], false);
      b.setOnClickListener(v -> showTab(index));
      tabs.add(b);
      LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(38), 1f);
      lp.setMargins(i == 0 ? 0 : dp(4), 0, 0, 0);
      tabRow.addView(b, lp);
    }
    panel.addView(tabRow);
    ScrollView scroll = new ScrollView(this);
    content = new LinearLayout(this);
    content.setOrientation(LinearLayout.VERTICAL);
    scroll.addView(content);
    panel.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

    // ---- the right side: the 3D view with its bars
    FrameLayout stage = new FrameLayout(this);
    root.addView(stage, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 0.64f));
    view = new GLSurfaceView(this);
    view.setEGLContextClientVersion(3);
    view.setEGLConfigChooser(8, 8, 8, 8, 24, 0);
    view.setPreserveEGLContextOnPause(true);
    view.setRenderer(new GLSurfaceView.Renderer() {
      @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) { StudioBridge.surfaceCreated(); }
      @Override public void onSurfaceChanged(GL10 gl, int w, int h) { width = w; height = h; }
      @Override public void onDrawFrame(GL10 gl) { drawFrame(); }
    });
    stage.addView(view, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT));
    touch();

    LinearLayout top = new LinearLayout(this);
    top.setOrientation(LinearLayout.VERTICAL);
    top.setPadding(dp(14), dp(10), dp(14), dp(10));
    title = text("Choose a model on the left", 15, TEXT, true);
    info = text("", 12, DIM, false);
    top.addView(title);
    top.addView(info);
    stage.addView(top, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP));

    LinearLayout bar = new LinearLayout(this);
    bar.setOrientation(LinearLayout.VERTICAL);
    bar.setPadding(dp(12), dp(6), dp(12), dp(10));
    bar.setBackground(round(0xCC14161B, 0));
    LinearLayout timeline = new LinearLayout(this);
    timeline.setGravity(Gravity.CENTER_VERTICAL);
    play = pill("Pause", false);
    play.setOnClickListener(v -> { playing = !playing; play.setText(playing ? "Pause" : "Play"); });
    timeline.addView(play, new LinearLayout.LayoutParams(dp(78), dp(36)));
    seek = new SeekBar(this);
    seek.setMax(1000);
    seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
      @Override public void onProgressChanged(SeekBar s, int p, boolean user) {
        if (user && length > 0) frame = length * p / 1000f;
      }
      @Override public void onStartTrackingTouch(SeekBar s) { seeking = true; }
      @Override public void onStopTrackingTouch(SeekBar s) { seeking = false; }
    });
    timeline.addView(seek, new LinearLayout.LayoutParams(0, dp(36), 1f));
    frameLabel = text("0 / 0", 12, DIM, false);
    frameLabel.setMinWidth(dp(70));
    frameLabel.setGravity(Gravity.END);
    timeline.addView(frameLabel);
    bar.addView(timeline);
    HorizontalScrollView optionsScroll = new HorizontalScrollView(this);
    LinearLayout options = new LinearLayout(this);
    options.setPadding(0, dp(6), 0, 0);
    optionsScroll.addView(options);
    toggle(options, "Grid", 1);
    toggle(options, "Axes", 2);
    toggle(options, "Bones", 4);
    toggle(options, "Textures", 8);
    Button inPlace = pill("In place ✓", true);
    inPlace.setOnClickListener(v -> {
      boolean on = !inPlace.isSelected();
      inPlace.setSelected(on);
      style(inPlace, on);
      inPlace.setText(on ? "In place ✓" : "In place");
      StudioBridge.setInPlace(on);
    });
    options.addView(inPlace, chip());
    Button speedButton = pill("Speed 1×", false);
    speedButton.setOnClickListener(v -> {
      speed = speed >= 2f ? 0.25f : speed * 2f;
      speedButton.setText(String.format(Locale.ROOT, "Speed %s×", speed == 0.25f ? "¼" : speed == 0.5f ? "½"
          : String.valueOf((int) speed)));
    });
    options.addView(speedButton, chip());
    Button reset = pill("Frame model", false);
    reset.setOnClickListener(v -> frameModel());
    options.addView(reset, chip());
    bar.addView(optionsScroll);
    FrameLayout.LayoutParams barLp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM);
    stage.addView(bar, barLp);

    setContentView(root);
    showTab(0);
    main.postDelayed(ticker, 100);
  }

  private volatile int width = 1, height = 1;

  // ---- the GL thread: the clock of the animation and the picture
  private void drawFrame() {
    long now = System.nanoTime();
    float dt = lastNanos == 0 ? 0 : (now - lastNanos) / 1e9f;
    lastNanos = now;
    if (dt > 0.1f) dt = 0.1f;
    if (playing && !seeking && length > 0) {
      frame += dt * 30f * speed;  // the game's motions: 30 frames a second
      if (frame > length) frame = 0;
    }
    StudioBridge.draw(width, height, yaw, pitch, distance, tx, ty, tz, flags, frame);
  }

  private final Runnable ticker = new Runnable() {
    @Override public void run() {
      if (length > 0) {
        if (!seeking) seek.setProgress((int) (frame / length * 1000f));
        frameLabel.setText(String.format(Locale.ROOT, "%d / %d", (int) frame, (int) length));
      } else {
        frameLabel.setText("rest pose");
      }
      main.postDelayed(this, 100);
    }
  };

  // ---- touch: one finger turns, two fingers move, a pinch zooms
  private float lastX, lastY;
  private int lastCount;

  @SuppressLint("ClickableViewAccessibility")
  private void touch() {
    ScaleGestureDetector scale = new ScaleGestureDetector(this, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
      @Override public boolean onScale(ScaleGestureDetector d) {
        float f = d.getScaleFactor();
        if (f > 0.01f) distance = Math.max(150f, Math.min(40000f, distance / f));
        return true;
      }
    });
    view.setOnTouchListener((v, e) -> {
      scale.onTouchEvent(e);
      int n = e.getPointerCount();
      float x = 0, y = 0;
      for (int i = 0; i < n; i++) { x += e.getX(i); y += e.getY(i); }
      x /= n;
      y /= n;
      if (e.getActionMasked() == MotionEvent.ACTION_DOWN || n != lastCount
          || e.getActionMasked() == MotionEvent.ACTION_POINTER_UP) {
        lastX = x;
        lastY = y;
        lastCount = n;
        return true;
      }
      if (e.getActionMasked() == MotionEvent.ACTION_MOVE) {
        float dx = x - lastX, dy = y - lastY;
        if (n == 1) {
          yaw -= dx * 0.008f;
          pitch = Math.max(-1.45f, Math.min(1.45f, pitch + dy * 0.006f));
        } else if (!scale.isInProgress() || n >= 2) {
          // move the target in the view's plane: right = (cos yaw, 0, -sin yaw), up = y
          float k = distance / Math.max(1, v.getHeight()) * 1.1f;
          tx -= dx * k * (float) Math.cos(yaw);
          tz += dx * k * (float) Math.sin(yaw);
          ty += dy * k;
        }
        lastX = x;
        lastY = y;
      }
      return true;
    });
  }

  private void frameModel() {
    float[] f = StudioBridge.frameModel();
    tx = f[0];
    ty = f[1];
    tz = f[2];
    distance = f[3];
    yaw = 0.6f;
    pitch = 0.2f;
  }

  // ---- the tabs
  private void showTab(int index) {
    tab = index;
    for (int i = 0; i < tabs.size(); i++) style(tabs.get(i), i == index);
    content.removeAllViews();
    if (files == null) {
      content.addView(note("CubeEvil4 Studio reads the discs and mod packs of CubeEvil4 Mods (or CubeEvil4). Install it, "
          + "import your discs there, then open the Studio again."));
      return;
    }
    if (index == 0) showModels();
    else if (index == 1) showAnimations();
    else if (index == 2) showConvert();
    else showEdit();
  }

  private void showModels() {
    LinearLayout sources = row();
    for (String s : new String[] {"disc1", "disc2", "mods"}) {
      Button b = pill(s.equals("disc1") ? "Disc 1" : s.equals("disc2") ? "Disc 2" : "Mod packs", s.equals(source));
      b.setOnClickListener(v -> { source = s; archivePath = null; showTab(0); });
      sources.addView(b, chip());
    }
    content.addView(sources);
    if (archivePath == null) {
      LinearLayout kinds = row();
      String[][] k = {{"all", "All"}, {"pl", "Characters"}, {"em", "Enemies"}, {"wep", "Weapons"}, {"other", "Other"}};
      for (String[] e : k) {
        Button b = pill(e[1], e[0].equals(kind));
        b.setOnClickListener(v -> { kind = e[0]; showTab(0); });
        kinds.addView(b, chip());
      }
      HorizontalScrollView hs = new HorizontalScrollView(this);
      hs.addView(kinds);
      content.addView(hs);
      List<String[]> found = archives();
      if (found.isEmpty()) {
        content.addView(note(!files.lastError.isEmpty()
            ? "The files of " + files.from() + " cannot be read: " + files.lastError
            : source.equals("mods") ? "No mod pack holds models (em/*.drs)."
            : "No archives here: import your discs in CubeEvil4 first."));
      }
      for (String[] f : found) {
        // f: rel, file name, pack name ("" none), size
        String parent = f[2].isEmpty() ? "" : f[2] + " · ";
        Button b = card(parent + f[1], String.format(Locale.ROOT, "%.1f MB", Long.parseLong(f[3]) / 1048576.0));
        b.setOnClickListener(v -> openArchive(f[0], f[1]));
        content.addView(b, item());
      }
      return;
    }
    Button back = pill("‹ All archives", false);
    back.setOnClickListener(v -> { archivePath = null; showTab(0); });
    content.addView(back, item());
    Button whole = pill("Whole model again", false);
    whole.setOnClickListener(v -> loadComplete());
    content.addView(whole, item());
    // the pieces on the model: tap one to see it tinted, "Off" takes it off (DevJunior, 19.11.2026: "it is hard to
    // know what you are changing")
    TextView onHead = text("On the model", 14, TEXT, true);
    onHead.setPadding(0, dp(8), 0, dp(2));
    content.addView(onHead);
    content.addView(note("Tap a part to see it highlighted in the view."));
    String[] shownParts = StudioBridge.parts();
    java.util.Set<Integer> onModel = new java.util.HashSet<>();
    for (int k = 0; k < shownParts.length; k++) {
      String[] p = shownParts[k].split("\t", 2);
      final int piece = k;
      final int entry = Integer.parseInt(p[0]);
      onModel.add(entry);
      LinearLayout line = row();
      Button b = card(p.length > 1 ? p[1] : "#" + entry, k == 0 ? "main model · #" + entry : "#" + entry);
      if (highlighted == k) b.setBackground(round(0xFF3A2F1C, ACCENT));
      b.setOnClickListener(v -> {
        highlighted = highlighted == piece ? -1 : piece;
        StudioBridge.highlight(highlighted);
        showTab(0);
      });
      line.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
      if (k > 0) {
        Button off = pill("Off", false);
        off.setOnClickListener(v -> {
          String r = StudioBridge.togglePiece(entry);
          if (!r.equals("removed")) toast("#" + entry + ": " + r);
          highlighted = -1;
          StudioBridge.highlight(-1);
          updateInfo();
          showTab(0);
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(64), dp(48));
        lp.setMargins(dp(6), 0, 0, 0);
        line.addView(off, lp);
      }
      content.addView(line, item());
    }
    // the archive's other models: on the model, or alone
    java.util.Map<Integer, String> names = new java.util.HashMap<>();
    for (String n : StudioBridge.partNames()) {
      String[] p = n.split("\t", 2);
      if (p.length == 2) names.put(Integer.parseInt(p[0]), p[1]);
    }
    TextView moreHead = text("More in this archive", 14, TEXT, true);
    moreHead.setPadding(0, dp(12), 0, dp(2));
    content.addView(moreHead);
    content.addView(note("Weapons, other hands, faces and outfits: “Add” puts one on the model (when it fits its "
        + "skeleton), “Alone” shows it by itself."));
    String[] entries = StudioBridge.entries();
    int shown = 0;
    for (String e : entries) {
      String[] p = e.split("\t");
      if (p.length < 3 || !p[1].startsWith("BIN") || p[2].equals("0")) continue;
      final int index = Integer.parseInt(p[0]);
      if (onModel.contains(index)) continue;
      LinearLayout line = row();
      String label = names.containsKey(index) ? names.get(index) : "Model #" + index;
      Button b = card(label, String.format(Locale.ROOT, "#%d · %d KB", index, Long.parseLong(p[2]) / 1024));
      b.setClickable(false);
      line.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
      Button add = pill("Add", false);
      add.setOnClickListener(v -> {
        if (!modelLoaded) { toast("Open a model first"); return; }
        String r = StudioBridge.togglePiece(index);
        if (!r.equals("added")) toast(label + ": " + r);
        updateInfo();
        showTab(0);
      });
      Button alone = pill("Alone", false);
      alone.setOnClickListener(v -> loadModel(index, label));
      LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(58), dp(48));
      lp.setMargins(dp(6), 0, 0, 0);
      line.addView(add, lp);
      LinearLayout.LayoutParams lp2 = new LinearLayout.LayoutParams(dp(64), dp(48));
      lp2.setMargins(dp(4), 0, 0, 0);
      line.addView(alone, lp2);
      content.addView(line, item());
      shown++;
    }
    if (shown == 0) content.addView(note("Nothing else: every model of this archive is on the view."));
  }

  private void showAnimations() {
    if (!modelLoaded) {
      content.addView(note("Choose a model first (Models tab)."));
      return;
    }
    content.addView(note("Sources: the model's own archive is in use. Others (Leon, a Ganado, your mod packs) play "
        + "on this model when they fit its skeleton."));
    LinearLayout sources = new LinearLayout(this);
    sources.setOrientation(LinearLayout.VERTICAL);
    addSource(sources, "This model's archive", archivePath);
    addSource(sources, "Leon (pl00)", discFile("em/pl00.drs"));
    addSource(sources, "Ganado (em10)", discFile("em/em10.drs"));
    for (String[] f : packMotions()) addSource(sources, f[1], f[0]);
    content.addView(sources);
    TextView head = text("Animations", 14, TEXT, true);
    head.setPadding(0, dp(10), 0, dp(4));
    content.addView(head);
    String[] names = StudioBridge.motionNames();
    if (names.length == 0) content.addView(note("No animations in this source."));
    Button rest = card("Rest pose", "the model as it is built");
    rest.setOnClickListener(v -> pickMotion(-1, "Rest pose"));
    content.addView(rest, item());
    for (int i = 0; i < names.length; i++) {
      final int clip = i;
      final String label = names[i];
      Button b = card(label, "");
      b.setOnClickListener(v -> pickMotion(clip, label));
      content.addView(b, item());
    }
  }

  private void addSource(LinearLayout list, String label, String rel) {
    if (rel == null || files.size(rel) < 0) return;
    Button b = pill(label, false);
    b.setOnClickListener(v -> {
      String path = files.path(rel);
      if (path == null) { toast(label + ": cannot be read"); return; }
      int n = StudioBridge.useMotions(path, rel.substring(rel.lastIndexOf('/') + 1));
      if (n < 0) toast(label + ": " + StudioBridge.lastError());
      else toast(label + ": " + n + " animations");
      length = 0;
      frame = 0;
      showTab(1);
    });
    list.addView(b, item());
  }

  private void showConvert() {
    content.addView(note("Coming next: import files of the PC releases (UHD .udas / .pmd, the 2023 remake's "
        + ".motlist and .mesh) and turn them into a mod pack, with a preview here before it goes into the game."));
    content.addView(note("Today these conversions run with the PC tools of the project (uhd_skin_convert, "
        + "pmd_weapon_convert, re9_finisher_pack); they move into the app step by step."));
  }

  private void showEdit() {
    content.addView(note("Coming next: pick a bone on the model, move / turn / scale it along X, Y and Z over the grid, "
        + "and save the result as a mod pack."));
  }

  // ---- actions
  // an archive opened shows its whole model at once, as the game puts it together (DevJunior, 19.11.2026: "the models
  // should open complete, easily")
  private void openArchive(String rel, String name) {
    String path = files.path(rel);
    String why = path == null ? "cannot be read" : StudioBridge.openArchive(path, name);
    if (!why.isEmpty()) {
      toast(name + ": " + why);
      return;
    }
    archivePath = rel;
    archiveName = name;
    loadComplete();
  }

  private void loadComplete() {
    String why = StudioBridge.loadComplete();
    highlighted = -1;
    if (!why.isEmpty()) {
      toast(archiveName + ": " + why);
      modelLoaded = false;
      showTab(0);
      return;
    }
    modelLoaded = true;
    length = 0;
    frame = 0;
    title.setText(archiveName);
    updateInfo();
    frameModel();
    showTab(0);
  }

  private void loadModel(int index, String label) {
    String why = StudioBridge.loadModel(index, -1);
    highlighted = -1;
    if (!why.isEmpty()) {
      toast(label + ": " + why);
      return;
    }
    modelLoaded = true;
    length = 0;
    frame = 0;
    title.setText(archiveName + " · " + label + " alone");
    updateInfo();
    frameModel();
    showTab(0);
  }

  private void pickMotion(int clip, String label) {
    String why = StudioBridge.setMotion(clip);
    if (!why.isEmpty()) {
      toast(label + ": " + why);
      return;
    }
    frame = 0;
    length = StudioBridge.motionLength();
    playing = true;
    play.setText("Pause");
    updateInfo();
    info.setText(info.getText() + (clip >= 0 ? " · " + label : ""));
  }

  private void updateInfo() { info.setText(StudioBridge.info()); }

  // ---- files (below the game's folder game/, StudioFiles)
  // the archives of the source: rel, file name, pack name ("" none), size
  private List<String[]> archives() {
    List<String[]> out = new ArrayList<>();
    files.lastError = "";
    if (source.equals("mods")) {
      for (StudioFiles.Item p : files.list("mods")) {
        if (!p.dir) continue;
        collect("mods/" + p.name + "/em", p.name, out);
        collect("mods/" + p.name + "/files/em", p.name, out);
      }
    } else {
      collect(source + "/files/em", "", out);
    }
    List<String[]> kept = new ArrayList<>();
    for (String[] f : out) {
      String n = f[1].toLowerCase(Locale.ROOT);
      boolean pl = n.startsWith("pl"), em = n.startsWith("em"), wep = n.startsWith("wep");
      if (kind.equals("all") || (kind.equals("pl") && pl) || (kind.equals("em") && em) || (kind.equals("wep") && wep)
          || (kind.equals("other") && !pl && !em && !wep)) kept.add(f);
    }
    return kept;
  }

  private void collect(String dir, String pack, List<String[]> out) {
    for (StudioFiles.Item f : files.list(dir)) {
      String n = f.name.toLowerCase(Locale.ROOT);
      if (!f.dir && (n.endsWith(".drs") || n.endsWith(".das"))) {
        out.add(new String[] {dir + "/" + f.name, f.name, pack, String.valueOf(f.size)});
      }
    }
  }

  private String discFile(String rel) {
    for (String disc : new String[] {"disc1", "disc2"}) {
      String r = disc + "/files/" + rel;
      if (files.size(r) >= 0) return r;
    }
    return null;
  }

  // the mod packs' animation archives (melee/*.dat: moves, reactions, finishers, kicks): rel, label
  private List<String[]> packMotions() {
    List<String[]> out = new ArrayList<>();
    for (StudioFiles.Item p : files.list("mods")) {
      if (!p.dir) continue;
      for (StudioFiles.Item f : files.list("mods/" + p.name + "/melee")) {
        if (!f.dir && f.name.toLowerCase(Locale.ROOT).endsWith(".dat")) {
          out.add(new String[] {"mods/" + p.name + "/melee/" + f.name, p.name + " · " + f.name});
        }
      }
    }
    return out;
  }

  // ---- the look
  private int dp(float v) {
    return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()));
  }

  private TextView text(String s, float sp, int color, boolean bold) {
    TextView t = new TextView(this);
    t.setText(s);
    t.setTextSize(sp);
    t.setTextColor(color);
    if (bold) t.setTypeface(t.getTypeface(), android.graphics.Typeface.BOLD);
    return t;
  }

  private TextView note(String s) {
    TextView t = text(s, 12, DIM, false);
    t.setPadding(dp(2), dp(6), dp(2), dp(8));
    return t;
  }

  private GradientDrawable round(int color, int stroke) {
    GradientDrawable g = new GradientDrawable();
    g.setColor(color);
    g.setCornerRadius(dp(12));
    if (stroke != 0) g.setStroke(dp(1), stroke);
    return g;
  }

  private Button pill(String s, boolean on) {
    Button b = new Button(this);
    b.setText(s);
    b.setAllCaps(false);
    b.setTextSize(13);
    b.setPadding(dp(12), 0, dp(12), 0);
    b.setMinHeight(0);
    b.setMinimumHeight(0);
    b.setMinWidth(0);
    b.setMinimumWidth(0);
    style(b, on);
    return b;
  }

  private void style(Button b, boolean on) {
    b.setSelected(on);
    b.setTextColor(on ? 0xFF15171B : TEXT);
    b.setBackground(round(on ? ACCENT : CARD, on ? 0 : 0xFF2C313A));
  }

  private Button card(String head, String detail) {
    Button b = new Button(this);
    b.setAllCaps(false);
    b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
    b.setText(detail.isEmpty() ? head : head + "\n" + detail);
    b.setTextSize(13);
    b.setTextColor(TEXT);
    b.setPadding(dp(14), dp(10), dp(14), dp(10));
    b.setBackground(round(CARD, 0xFF2C313A));
    return b;
  }

  private void toggle(LinearLayout row, String label, int bit) {
    Button b = pill(label, (flags & bit) != 0);
    b.setOnClickListener(v -> {
      flags ^= bit;
      style(b, (flags & bit) != 0);
    });
    row.addView(b, chip());
  }

  private LinearLayout row() {
    LinearLayout r = new LinearLayout(this);
    r.setOrientation(LinearLayout.HORIZONTAL);
    r.setGravity(Gravity.CENTER_VERTICAL);
    r.setPadding(0, dp(2), 0, dp(4));
    return r;
  }

  private LinearLayout.LayoutParams chip() {
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(34));
    lp.setMargins(0, 0, dp(6), 0);
    return lp;
  }

  private LinearLayout.LayoutParams item() {
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT);
    lp.setMargins(0, dp(3), 0, dp(3));
    return lp;
  }

  private void toast(String s) {
    Log.i(TAG, s);
    Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
  }

  @Override protected void onResume() {
    super.onResume();
    view.onResume();
    lastNanos = 0;
  }

  @Override protected void onPause() {
    view.onPause();
    super.onPause();
  }

  @Override protected void onDestroy() {
    main.removeCallbacks(ticker);
    if (files != null) files.close();
    super.onDestroy();
  }
}
