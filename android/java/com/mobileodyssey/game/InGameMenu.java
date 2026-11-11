/* InGameMenu - 0.2.3: the menu the touch Exit button and the controller's Exit key open (it replaces the "Leave the game?"
 * question). A panel on the right in the app's own look (LauncherActivity's colours and widgets), the game held and still
 * visible on the left, so a change to the picture can be compared at once.
 * What it changes takes effect without a new start:
 *   graphics.ini: upscaling, its sharpness (sharpening / fsr_sharpness / gsr_sharpness), anti_aliasing, fps_counter.
 *     Written here, then GameActivity.nativeReloadOptions (gfx_options_reload_live) reads them again; the game runs for a
 *     moment (PREVIEW_MS) so the held picture is drawn again with the change.
 *   controls.ini: touch, touch_opacity, touch_scale, camera_speed. The game reads the file again within a second
 *     (nnpad_android.c nnpad_poll).
 * Some settings only take effect on a new start (resolution_scale, console_mode, anisotropic_filtering): they
 * stand with the others and are written as the app's Graphics tab writes them; once one differs from what this game was
 * started with, a note above the buttons says so, with "Restart now".
 *   The performance overlay (fps_counter, fps_average, fps_low, frame_time, fps_size) is drawn by the present pass like the
 * rest of the picture settings, and changes at once.
 * Restart: as Exit, and the app's menu starts the game again (LauncherActivity.restartIfAsked). Exit and Restart take a
 * second tap. */
package com.mobileodyssey.game;

import android.app.Dialog;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import java.io.File;

import static com.mobileodyssey.game.LauncherActivity.ACCENT;
import static com.mobileodyssey.game.LauncherActivity.ACCENT2;
import static com.mobileodyssey.game.LauncherActivity.BG;
import static com.mobileodyssey.game.LauncherActivity.CARD;
import static com.mobileodyssey.game.LauncherActivity.CARD2;
import static com.mobileodyssey.game.LauncherActivity.DIM;
import static com.mobileodyssey.game.LauncherActivity.TEXT;
import static com.mobileodyssey.game.LauncherActivity.WARN;

class InGameMenu {
    static final int PREVIEW_MS = 150;                     /* the game runs this long after a picture change: about 9 frames */
    final GameActivity a;
    final android.content.Context ui;                      /* the views' context: the launcher's theme (manifest: Theme.DeviceDefault.NoActionBar) */
    final LauncherActivity.Ini gfx, ctl;
    Dialog dialog;
    LinearLayout content;
    boolean leaving;
    private Runnable rehold;
    static final String[][] RESTART_KEYS = {{"resolution_scale", "1.00"}, {"console_mode", "docked"}, {"anisotropic_filtering", "game"}};
    private final String[] started = new String[RESTART_KEYS.length];      /* their values when this game started (the menu is made on its first opening) */
    private LinearLayout restartNote;
    private Button exitBtn, restartBtn, resumeBtn;

    InGameMenu(GameActivity a) {
        this.a = a;
        ui = new android.view.ContextThemeWrapper(a, android.R.style.Theme_DeviceDefault_NoActionBar);
        File config = new File(a.getExternalFilesDir(null), "config");
        gfx = new LauncherActivity.Ini(new File(config, "graphics.ini"));
        ctl = new LauncherActivity.Ini(new File(config, "controls.ini"));
        for (int i = 0; i < RESTART_KEYS.length; i++) started[i] = gfx.get(RESTART_KEYS[i][0], RESTART_KEYS[i][1]);
    }
    private boolean restartNeeded() {
        for (int i = 0; i < RESTART_KEYS.length; i++) if (!started[i].equalsIgnoreCase(gfx.get(RESTART_KEYS[i][0], RESTART_KEYS[i][1]))) return true;
        return false;
    }

    boolean showing() { return dialog != null; }

    void show() {
        if (dialog != null || a.isFinishing()) return;
        a.nativeHold(true);
        try { a.nativeMenuOpen(true); } catch (UnsatisfiedLinkError e) { }      /* the game is not "in the background" while this has the focus */
        LinearLayout panel = new LinearLayout(ui);
        panel.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(BG);
        float r = dp(22);
        bg.setCornerRadii(new float[]{r, r, 0, 0, 0, 0, r, r});
        panel.setBackground(bg);
        panel.setPadding(dp(20), dp(18), dp(20), dp(14));

        LinearLayout head = new LinearLayout(ui);
        head.setOrientation(LinearLayout.VERTICAL);
        head.addView(text("Paused", 26, TEXT, true));
        TextView info = text(pictureInfo(), 12, DIM, false);
        info.setPadding(0, dp(2), 0, dp(8));
        head.addView(info);
        panel.addView(head);

        ScrollView sv = new ScrollView(ui);
        sv.setVerticalScrollBarEnabled(false);
        content = new LinearLayout(ui);
        content.setOrientation(LinearLayout.VERTICAL);
        sv.addView(content);
        panel.addView(sv, new LinearLayout.LayoutParams(-1, 0, 1));
        build();

        restartNote = new LinearLayout(ui);
        restartNote.setOrientation(LinearLayout.HORIZONTAL);
        restartNote.setGravity(Gravity.CENTER_VERTICAL);
        restartNote.setPadding(dp(14), dp(8), dp(8), dp(8));
        restartNote.setBackground(round(0x33F5A524, 12));
        TextView rn = text("One or more changed options need a restart.", 13, WARN, true);
        restartNote.addView(rn, new LinearLayout.LayoutParams(0, -2, 1));
        Button now = button("Restart now", CARD2);
        now.setTextSize(13);
        now.setOnClickListener(v -> doLeave(true));
        restartNote.addView(now, new LinearLayout.LayoutParams(-2, dp(38)));
        LinearLayout.LayoutParams nlp = new LinearLayout.LayoutParams(-1, -2);
        nlp.setMargins(0, dp(10), 0, 0);
        panel.addView(restartNote, nlp);
        restartNote.setVisibility(restartNeeded() ? View.VISIBLE : View.GONE);

        LinearLayout row = new LinearLayout(ui);             /* one row: the settings get the height of a phone held sideways */
        row.setPadding(0, dp(10), 0, 0);
        resumeBtn = button("Resume", ACCENT2);
        restartBtn = button("Restart", CARD2);
        exitBtn = button("Exit", ACCENT);
        row.addView(resumeBtn, new LinearLayout.LayoutParams(0, dp(46), 1.2f));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(46), 1);
        lp.setMargins(dp(8), 0, 0, 0);
        row.addView(restartBtn, lp);
        lp = new LinearLayout.LayoutParams(0, dp(46), 1);
        lp.setMargins(dp(8), 0, 0, 0);
        row.addView(exitBtn, lp);
        panel.addView(row);

        Dialog d = new Dialog(a);
        d.requestWindowFeature(Window.FEATURE_NO_TITLE);
        d.setContentView(panel);
        d.setCanceledOnTouchOutside(true);
        Window w = d.getWindow();
        w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        int sw = a.getResources().getDisplayMetrics().widthPixels;
        w.setLayout(Math.min(dp(400), sw - dp(32)), WindowManager.LayoutParams.MATCH_PARENT);
        w.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        w.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
        w.setDimAmount(0.25f);                             /* the game stays visible: a change to the picture is seen at once */
        WindowManager.LayoutParams wl = w.getAttributes();
        wl.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        w.setAttributes(wl);
        resumeBtn.setOnClickListener(v -> d.dismiss());
        twoTap(restartBtn, "Restart", "Again?", () -> doLeave(true));
        twoTap(exitBtn, "Exit", "Again?", () -> doLeave(false));
        d.setOnDismissListener(x -> {
            if (rehold != null) a.ui.removeCallbacks(rehold);
            dialog = null;
            try { a.nativeMenuOpen(false); } catch (UnsatisfiedLinkError e) { }
            if (leaving) return;
            a.nativeHold(false);
            GameActivity.fullScreen(a);
        });
        dialog = d;
        w.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);      /* not focusable while it is shown, or the system bars come back with it */
        d.show();
        WindowInsetsController c = w.getDecorView().getWindowInsetsController();
        if (c != null) { c.hide(WindowInsets.Type.systemBars()); c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE); }
        w.clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
    }

    /* a button that asks for a second tap within 3 s */
    private void twoTap(Button b, String label, String again, Runnable go) {
        Runnable[] undo = new Runnable[1];
        b.setOnClickListener(v -> {
            if (undo[0] != null) { a.ui.removeCallbacks(undo[0]); undo[0] = null; go.run(); return; }
            b.setText(again);
            undo[0] = () -> { undo[0] = null; b.setText(label); };
            a.ui.postDelayed(undo[0], 3000);
        });
    }
    private void doLeave(boolean restart) {
        if (leaving) return;
        leaving = true;
        (restart ? restartBtn : exitBtn).setText(restart ? "Restarting\u2026" : "Leaving\u2026");
        exitBtn.setEnabled(false); restartBtn.setEnabled(false); resumeBtn.setEnabled(false);
        if (restartNote != null) restartNote.setVisibility(View.GONE);
        a.leave(restart);
    }

    private String pictureInfo() {
        int[] v = new int[5];
        try { a.nativeMenuInfo(v); } catch (UnsatisfiedLinkError e) { return ""; }
        if (v[0] <= 0 || v[2] <= 0) return "";
        String s = "Game picture " + v[0] + "×" + v[1] + " → " + v[2] + "×" + v[3] + " on screen";
        if (v[2] > v[0] + 1 || v[3] > v[1] + 1) s += String.format(java.util.Locale.ROOT, " (%.1f× upscale)", (float)v[3] / v[1]);
        else if (v[2] < v[0] - 1) s += " (scaled down)";
        return s;
    }

    /* the settings, built again after each choice so the sharpness slider follows the upscaler */
    private void build() {
        content.removeAllViews();
        LinearLayout g = card();
        g.addView(header("Graphics"));
        boolean hh = "handheld".equalsIgnoreCase(gfx.get("console_mode", "docked"));
        int bw = hh ? 1280 : 1600, bh = hh ? 720 : 900;
        String rv = gfx.get("resolution_scale", "1.00");
        float curScale = "auto".equalsIgnoreCase(rv) ? (hh ? 1f : 1.2f) : LauncherActivity.parse(rv, 1f);
        g.addView(choice(gfx, "console_mode", "Console mode", new String[]{"docked", "handheld"}, new String[]{"Docked", "Handheld"}, "docked", false));      /* restart */
        g.addView(choice(gfx, "resolution_scale", "Resolution", new String[]{"auto", "1.50", "2.00"},                     /* restart */
                new String[]{"Native\n" + (hh ? "1280x720" : "1920x1080"), "1.5x\n" + LauncherActivity.size(bw, bh, 1.5f), "2x\n" + LauncherActivity.size(bw, bh, 2f)}, "1.00", false));
        g.addView(resSlider(bw, bh, Math.round(curScale * 100)));                                   /* restart */
        String up = gfx.get("upscaling", "bilinear").toLowerCase(java.util.Locale.ROOT);
        g.addView(choice(gfx, "upscaling", "Upscaler", new String[]{"bilinear", "bicubic", "fsr", "gsr"},
                new String[]{"Bilinear", "Bicubic", "FSR 1", "GSR"}, "bilinear", true));      /* the app's longer names wrap in this panel */
        if (up.equals("fsr")) g.addView(slider(gfx, "fsr_sharpness", "FSR sharpness", 0, 100, 5, 90, "%", 1f, true));
        else if (up.equals("gsr")) g.addView(slider(gfx, "gsr_sharpness", "GSR sharpness", 0, 100, 5, 50, "%", 1f, true));
        else g.addView(slider(gfx, "sharpening", "Sharpening", 0, 100, 5, 75, "%", 1f, true));
        g.addView(choice(gfx, "anti_aliasing", "Anti-aliasing", new String[]{"off", "fxaa"}, new String[]{"Off", "FXAA"}, "fxaa", true));
        if (!up.equals("bilinear")) g.addView(small("Where the picture is enlarged, " + (up.equals("bicubic") ? "Bicubic" : up.equals("fsr") ? "FSR" : "GSR") + " is used instead of FXAA."));
        g.addView(choice(gfx, "anisotropic_filtering", "Anisotropic filtering", new String[]{"game", "2", "4", "8", "16"},
                new String[]{"Game", "2x", "4x", "8x", "16x"}, "game", false));                     /* restart */
        g.addView(toggle(gfx, "steady_speed", "Steady game speed", "on", "off", "off", true));      /* 0.2.6: read again at once (gfx_options_reload_live) */
        g.addView(small("Keeps the game at normal speed when fps is below 60. Turn it on if you limit fps below 60."));
        g.addView(slider(gfx, "fps_cap", "FPS limit", 20, 60, 5, 60, "", 1f, true, "60 = no limit. Below 60, turn on Steady game speed."));      /* 0.2.8: live (gfx_options_reload_live) */
        fpsWarn = text("\u26A0 Turn on Steady game speed: with the FPS limit below 60 the game plays in slow motion without it.", 12, WARN, true);
        fpsWarn.setPadding(dp(4), dp(2), dp(4), dp(6));
        g.addView(fpsWarn);
        fpsWarnUpdate();
        content.addView(g);

        LinearLayout f = card();
        f.addView(header("Performance overlay"));
        f.addView(toggle(gfx, "fps_counter", "FPS", "on", "off", "on", true));
        f.addView(toggle(gfx, "fps_average", "Average FPS", "on", "off", "off", true));
        f.addView(toggle(gfx, "fps_low", "1% low", "on", "off", "off", true));
        f.addView(toggle(gfx, "frame_time", "Frame time", "on", "off", "off", true));
        f.addView(slider(gfx, "fps_size", "Overlay size", 50, 200, 10, 100, "%", 1f, true));
        f.addView(small("Average and 1% low are of the last 10 seconds or so."));
        content.addView(f);

        LinearLayout t = card();
        t.addView(header("Touch controls"));
        t.addView(choice(ctl, "touch", "Show", new String[]{"auto", "on", "off"}, new String[]{"Auto", "Always", "Off"}, "auto", false));
        t.addView(slider(ctl, "touch_opacity", "Opacity", 10, 100, 5, 45, "%", 0.01f, false));
        t.addView(slider(ctl, "touch_scale", "Size", 60, 160, 5, 100, "%", 0.01f, false));
        t.addView(slider(ctl, "camera_speed", "Camera speed", 20, 400, 10, 100, "%", 0.01f, false));
        content.addView(t);

        content.addView(small("Everything else is in the app's Graphics and Controls tabs."));
        noteRestart();
    }
    private void noteRestart() { if (restartNote != null) restartNote.setVisibility(restartNeeded() ? View.VISIBLE : View.GONE); }

    /* resolution_scale as the Graphics tab's "Custom scale", with the size it gives */
    private View resSlider(int bw, int bh, int cur) {
        final int min = 25, max = 200, step = 5;
        cur = Math.max(min, Math.min(max, cur));
        LinearLayout row = new LinearLayout(ui);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, dp(10), 0, dp(4));
        TextView title = text("Custom scale: " + cur + "%", 15, TEXT, false);
        row.addView(title);
        SeekBar sb = new SeekBar(ui);
        sb.setMax((max - min) / step);
        sb.setProgress((cur - min) / step);
        sb.setProgressTintList(ColorStateList.valueOf(ACCENT2));
        sb.setThumbTintList(ColorStateList.valueOf(ACCENT2));
        TextView help = text("Now " + LauncherActivity.size(bw, bh, cur / 100f) + ". Your own scale of the game's " + bw + "x" + bh + ".", 12, DIM, false);
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s, int pr, boolean u) {
                int v = min + pr * step;
                title.setText("Custom scale: " + v + "%");
                help.setText("Now " + LauncherActivity.size(bw, bh, v / 100f) + ". Your own scale of the game's " + bw + "x" + bh + ".");
            }
            public void onStartTrackingTouch(SeekBar s) { }
            public void onStopTrackingTouch(SeekBar s) {
                gfx.set("resolution_scale", String.format(java.util.Locale.ROOT, "%.2f", (min + s.getProgress() * step) / 100f));
                noteRestart();
            }
        });
        row.addView(sb);
        row.addView(help);
        return row;
    }

    /* 0.2.8: the yellow note under the FPS limit while it is below 60 and Steady game speed is off (as the launcher's) */
    private TextView fpsWarn;
    private void fpsWarnUpdate() {
        if (fpsWarn == null) return;
        float cap = LauncherActivity.parse(gfx.get("fps_cap", "60"), 60f);
        boolean st = "on".equalsIgnoreCase(gfx.get("steady_speed", "off"));
        fpsWarn.setVisibility(cap > 0 && cap < 59.5f && !st ? View.VISIBLE : View.GONE);
    }

    /* graphics.ini changed: the game reads it again, then runs for a moment so the held picture shows the change */
    private void applyPicture() {
        try { a.nativeReloadOptions(); } catch (UnsatisfiedLinkError e) { return; }
        a.nativeHold(false);
        if (rehold != null) a.ui.removeCallbacks(rehold);
        rehold = () -> { rehold = null; if (dialog != null && !leaving) a.nativeHold(true); };
        a.ui.postDelayed(rehold, PREVIEW_MS);
    }

    /* ------------------------------------------------------------ widgets, as LauncherActivity's */
    private View choice(LauncherActivity.Ini ini, String key, String label, String[] vals, String[] names, String def, boolean picture) {
        return choice(ini, key, label, vals, names, def, picture, null);
    }
    private View choice(LauncherActivity.Ini ini, String key, String label, String[] vals, String[] names, String def, boolean picture, String help) {
        LinearLayout row = new LinearLayout(ui);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, dp(10), 0, dp(4));
        row.addView(text(label, 15, TEXT, false));
        LinearLayout seg = new LinearLayout(ui);
        seg.setPadding(0, dp(6), 0, 0);
        String cur = ini.get(key, def);
        for (int i = 0; i < vals.length; i++) {
            final String v = vals[i];
            boolean on = v.equalsIgnoreCase(cur);
            TextView b = text(names[i], 13, on ? Color.WHITE : DIM, true);
            b.setGravity(Gravity.CENTER);
            b.setPadding(dp(10), dp(8), dp(10), dp(8));
            b.setBackground(round(on ? ACCENT2 : CARD2, 10));
            b.setOnClickListener(x -> { if (v.equalsIgnoreCase(ini.get(key, def))) return; ini.set(key, v); if (picture) applyPicture(); build(); });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, -2, 1);
            lp.setMargins(i == 0 ? 0 : dp(4), 0, 0, 0);
            seg.addView(b, lp);
        }
        row.addView(seg);
        if (help != null) row.addView(text(help, 12, DIM, false));
        return row;
    }

    private View slider(LauncherActivity.Ini ini, String key, String label, int min, int max, int step, int defPct, String unit, float scale, boolean picture) {
        return slider(ini, key, label, min, max, step, defPct, unit, scale, picture, null);
    }
    private View slider(LauncherActivity.Ini ini, String key, String label, int min, int max, int step, int defPct, String unit, float scale, boolean picture, String help) {
        LinearLayout row = new LinearLayout(ui);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, dp(10), 0, dp(4));
        int cur = Math.round(LauncherActivity.parse(ini.get(key, String.valueOf(defPct * scale)), defPct * scale) / scale);
        cur = Math.max(min, Math.min(max, cur));
        TextView title = text(label + ": " + cur + unit, 15, TEXT, false);
        row.addView(title);
        SeekBar sb = new SeekBar(ui);
        sb.setMax((max - min) / step);
        sb.setProgress((cur - min) / step);
        sb.setProgressTintList(ColorStateList.valueOf(ACCENT2));
        sb.setThumbTintList(ColorStateList.valueOf(ACCENT2));
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s, int pr, boolean u) { title.setText(label + ": " + (min + pr * step) + unit); }
            public void onStartTrackingTouch(SeekBar s) { }
            public void onStopTrackingTouch(SeekBar s) {
                ini.set(key, String.format(java.util.Locale.ROOT, "%.2f", (min + s.getProgress() * step) * scale));
                fpsWarnUpdate();
                if (picture) applyPicture();
            }
        });
        row.addView(sb);
        if (help != null) row.addView(text(help, 12, DIM, false));
        return row;
    }

    private View toggle(LauncherActivity.Ini ini, String key, String label, String on, String off, String def, boolean picture) {
        LinearLayout row = new LinearLayout(ui);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, dp(8), 0, dp(4));
        Switch s = new Switch(ui);
        s.setText(label);
        s.setTextColor(TEXT);
        s.setTextSize(15);
        s.setChecked(on.equalsIgnoreCase(ini.get(key, def)));
        LauncherActivity.styleSwitch(s, ui.getResources().getDisplayMetrics().density);
        s.setOnCheckedChangeListener((x, c) -> { ini.set(key, c ? on : off); fpsWarnUpdate(); if (picture) applyPicture(); });
        row.addView(s);
        return row;
    }

    private LinearLayout card() {
        LinearLayout c = new LinearLayout(ui);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setPadding(dp(18), dp(16), dp(18), dp(16));
        c.setBackground(round(CARD, 16));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.setMargins(0, dp(6), 0, dp(10));
        c.setLayoutParams(lp);
        return c;
    }
    private TextView header(String s) { TextView t = text(s.toUpperCase(java.util.Locale.ROOT), 12, ACCENT2, true); t.setLetterSpacing(0.08f); t.setPadding(0, 0, 0, dp(4)); return t; }
    private TextView small(String s) { TextView t = text(s, 12, DIM, false); t.setPadding(dp(4), dp(4), dp(4), dp(4)); return t; }
    private TextView text(String s, int sp, int col, boolean bold) {
        TextView t = new TextView(ui);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(col);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }
    private Button button(String s, int col) {
        Button b = new Button(ui);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextColor(Color.WHITE);
        b.setTextSize(15);
        b.setBackground(round(col, 14));
        return b;
    }
    private GradientDrawable round(int col, int r) { GradientDrawable g = new GradientDrawable(); g.setColor(col); g.setCornerRadius(dp(r)); return g; }
    private int dp(int v) { return Math.round(v * a.getResources().getDisplayMetrics().density); }
}
