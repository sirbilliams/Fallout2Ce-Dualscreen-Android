package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.widget.TextView;
import java.io.File;
import java.io.IOException;

// The safety net before Et Tu starts: unpacks Fallout 1's data if that is still
// to do (EtTuUndat), showing a status line, then opens the game. On failure the
// message stays on screen and the game is not started.
public class EtTuUndatActivity extends Activity {
    private static final String TAG = "EtTuUndatActivity";
    private TextView text;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        text = new TextView(this);
        text.setTextColor(Color.WHITE);
        text.setBackgroundColor(Color.BLACK);
        text.setTextSize(20);
        text.setGravity(Gravity.CENTER);
        text.setPadding(48, 48, 48, 48);
        text.setText("Unpacking Fallout 1 data for Et Tu...");
        setContentView(text);
        final File top = GameRoots.topRoot(this);
        new Thread(() -> {
            try {
                EtTuUndat.run(top, EtTuUndat.REAL);
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "unpack failed", e);
                String message = e instanceof IOException ? EtTuUndat.failureText((IOException) e)
                    : "Could not unpack the Fallout 1 data for Et Tu: " + e;
                runOnUiThread(() -> text.setText(message + "\n\nImport Fallout 1 again from the picker, or reinstall Et Tu."));
                return;
            }
            runOnUiThread(() -> {
                startActivity(new Intent(this, MainActivity.class));
                finish();
            });
        }).start();
    }
}
