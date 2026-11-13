package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;

import java.util.List;

// Runs in the :import process: waits until the game's process has exited
// (MainActivity.onDestroy), then opens the game again with fresh native state.
public class RestartActivity extends Activity {
    private static final long POLL_MS = 100;
    private static final long MAX_MS = 5000;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private long waited = 0;

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!mainProcessRunning() || waited >= MAX_MS) {
                startActivity(new Intent(RestartActivity.this, MainActivity.class));
                finish();
                return;
            }
            waited += POLL_MS;
            handler.postDelayed(this, POLL_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        handler.post(poll);
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(poll);
        super.onDestroy();
    }

    private boolean mainProcessRunning() {
        ActivityManager manager = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> list = manager != null ? manager.getRunningAppProcesses() : null;
        if (list == null) return false;
        for (ActivityManager.RunningAppProcessInfo info : list) {
            if (getPackageName().equals(info.processName)) return true;
        }
        return false;
    }
}
