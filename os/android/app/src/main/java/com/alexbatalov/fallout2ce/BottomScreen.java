package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;

// The second screen of a dual-screen device (AYN Thor and the like): the game
// stays on the activity's display (top), the mobile UI's panels go to the
// other one (bottom) through a Presentation whose surface native code draws
// into (bottom_screen.cc).
//
// Lives while the activity is resumed: `start` in onResume, `stop` in
// onPause. Follows the display coming and going (a dock, a cable, the
// device's own screen settings) through the display listener.
final class BottomScreen implements DisplayManager.DisplayListener {
    private static final String TAG = "BottomScreen";

    private final Activity activity;
    private final DisplayManager displayManager;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private Panel panel;
    private boolean started;

    BottomScreen(Activity activity) {
        this.activity = activity;
        this.displayManager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
    }

    void start() {
        if (started || displayManager == null) {
            return;
        }
        started = true;
        displayManager.registerDisplayListener(this, handler);
        show();
    }

    void stop() {
        if (!started) {
            return;
        }
        started = false;
        displayManager.unregisterDisplayListener(this);
        dismiss();
    }

    // The display for the panels: a presentation display that isn't the
    // game's, else any other display.
    private Display findDisplay() {
        int gameDisplayId = activity.getWindowManager().getDefaultDisplay().getDisplayId();

        for (Display display : displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.getDisplayId() != gameDisplayId) {
                return display;
            }
        }

        for (Display display : displayManager.getDisplays()) {
            if (display.getDisplayId() != gameDisplayId) {
                return display;
            }
        }

        return null;
    }

    private void show() {
        if (panel != null || activity.isFinishing()) {
            return;
        }

        Display display = findDisplay();
        if (display == null) {
            Log.i(TAG, "no second display, single screen");
            return;
        }

        Panel newPanel = new Panel(activity, display);
        try {
            newPanel.show();
        } catch (WindowManager.InvalidDisplayException e) {
            Log.w(TAG, "second display went away before the panel showed", e);
            return;
        }
        panel = newPanel;
        Log.i(TAG, "panel on display " + display.getDisplayId() + " (" + display.getName() + ")");
    }

    private void dismiss() {
        if (panel != null) {
            panel.dismiss();
            panel = null;
        }
    }

    @Override
    public void onDisplayAdded(int displayId) {
        if (started) {
            show();
        }
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        if (panel != null && panel.getDisplay().getDisplayId() == displayId) {
            dismiss();
        }
        if (started) {
            show();
        }
    }

    @Override
    public void onDisplayChanged(int displayId) {
    }

    // The bottom screen's window: one surface filling it, handed to native
    // code while it exists.
    private static final class Panel extends Presentation implements SurfaceHolder.Callback {
        Panel(Context outerContext, Display display) {
            super(outerContext, display);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);

            Window window = getWindow();
            if (window != null) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
            }

            SurfaceView view = new SurfaceView(getContext());
            view.getHolder().addCallback(this);
            setContentView(view);
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            nativeSurfaceChanged(holder.getSurface(), width, height);
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            nativeSurfaceDestroyed();
        }
    }

    // bottom_screen.cc
    static native void nativeSurfaceChanged(Surface surface, int width, int height);
    static native void nativeSurfaceDestroyed();
}
