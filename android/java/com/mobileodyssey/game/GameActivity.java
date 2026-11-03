/* GameActivity - the game's window: android.app.NativeActivity running libsmo.so (runtime/android/android_main.c),
 * with the system bars hidden. The theme alone hides only the status bar; phones with button navigation then keep
 * the navigation bar over the right edge in landscape, on top of the touch controls there (A, ZR, R). Immersive:
 * the bars come back for a moment with a swipe from the edge. The layout editor uses the same window (fullScreen()),
 * so a place there is the same place in the game. */
package com.mobileodyssey.game;

import android.app.Activity;
import android.app.NativeActivity;
import android.os.Bundle;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

public class GameActivity extends NativeActivity {
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        fullScreen(this);
    }
    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) fullScreen(this);                       /* again after a dialog, the notification shade, another app */
    }

    /* The whole display: under the system bars and the camera cutout, the bars hidden until a swipe. */
    static void fullScreen(Activity a) {
        Window w = a.getWindow();
        WindowManager.LayoutParams p = w.getAttributes();
        p.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        w.setAttributes(p);
        w.setDecorFitsSystemWindows(false);
        WindowInsetsController c = w.getDecorView().getWindowInsetsController();    /* getDecorView makes the view first: Window.getInsetsController alone fails before setContentView (Samsung, Android 16) */
        if (c == null) return;
        c.hide(WindowInsets.Type.systemBars());
        c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
    }
}
