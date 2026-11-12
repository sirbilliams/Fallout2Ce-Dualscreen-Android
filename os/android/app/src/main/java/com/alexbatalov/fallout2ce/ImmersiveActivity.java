package com.alexbatalov.fallout2ce;

import android.app.Activity;

// Java setup, import and mod screens share the game's fullscreen policy.
public abstract class ImmersiveActivity extends Activity {
    @Override
    protected void onResume() {
        super.onResume();
        ImmersiveMode.apply(getWindow());
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            ImmersiveMode.apply(getWindow());
        }
    }
}
