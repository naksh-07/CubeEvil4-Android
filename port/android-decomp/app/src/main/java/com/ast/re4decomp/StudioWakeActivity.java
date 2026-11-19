package com.ast.re4decomp;

import android.app.Activity;
import android.os.Bundle;

/**
 * Starts the process ":studiodata" (StudioDataProvider) for CubeEvil4 Studio's own APK and closes at once (DevJunior's
 * Xiaomi, Android 16, 19.11.2026: the Studio's call alone did not start the game app's process, the system keeps
 * other apps from starting it in the background; an activity the Studio opens while in front may). Nothing to see.
 */
public final class StudioWakeActivity extends Activity {
  @Override protected void onCreate(Bundle state) {
    super.onCreate(state);
    finish();
    overridePendingTransition(0, 0);
  }
}
