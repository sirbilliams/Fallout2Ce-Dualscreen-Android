package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.content.DialogInterface;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Surface;
import android.view.Window;
import android.view.WindowManager;

// The second screen of a dual-screen device (AYN Thor): the game stays on the
// activity's display (top), the mobile UI's panels go to the device's
// built-in bottom screen (BottomDisplay.java: never a TV, cast or HDMI
// display, so mirroring keeps showing the game) through a Presentation
// whose surface native code draws into (bottom_screen.cc).
//
// Lives while the activity is resumed: `start` in onResume, `stop` in
// onPause. Follows displays coming, going and changing through the display
// listener. The panel window never takes focus, so the gamepad and keys stay
// with the game; it still gets touches.
final class BottomScreen implements DisplayManager.DisplayListener {
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
        update("start");
    }

    void stop() {
        if (!started) {
            return;
        }
        started = false;
        displayManager.unregisterDisplayListener(this);
        update(null);
    }

    // Shows the panel on the bottom screen while started and it's there,
    // moves or hides it otherwise. [why]: the display log's reason, null for
    // none.
    private void update(String why) {
        Display display = started && !activity.isFinishing() ? BottomDisplay.find(activity, displayManager, why) : null;
        if (panel != null && (display == null || panel.getDisplay().getDisplayId() != display.getDisplayId())) {
            hide();
        }
        if (display != null && panel == null) {
            show(display);
        }
    }

    private void show(Display display) {
        final Panel newPanel = new Panel(activity, display);
        // A Presentation dismisses itself when its display goes away or
        // changes size: forget it and look again.
        newPanel.setOnDismissListener(new DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(DialogInterface dialog) {
                if (panel != newPanel) {
                    return;
                }
                panel = null;
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        update("panel closed");
                    }
                });
            }
        });
        try {
            newPanel.show();
        } catch (WindowManager.InvalidDisplayException | IllegalStateException e) {
            BottomDisplay.log(activity, "panel didn't show: " + e);
            return;
        }
        panel = newPanel;
    }

    private void hide() {
        Panel oldPanel = panel;
        // Before dismissing: the dismiss listener ignores it then.
        panel = null;
        try {
            oldPanel.dismiss();
        } catch (RuntimeException e) {
            // The display went away.
        }
    }

    @Override
    public void onDisplayAdded(int displayId) {
        update("display " + displayId + " added");
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        update("display " + displayId + " removed");
    }

    @Override
    public void onDisplayChanged(int displayId) {
        update(null);
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
                window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                    | WindowManager.LayoutParams.FLAG_FULLSCREEN);
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
