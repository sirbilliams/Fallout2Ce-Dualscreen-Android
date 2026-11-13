package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

// "Install RPU" (Mods screen footer): looks up the latest Restoration Project
// release on GitHub (never bundled in the app), shows its size, warns about
// mobile data and low space, then hands the zip to the background download
// manager (ModDownloads). It is installed from the Downloads screen.
// Same panel as ImportActivity.
public class RpuActivity extends ImmersiveActivity {
    static final String MODE = "rpu";
    private static final String TAG = "RpuActivity";

    private TextView title;
    private TextView message;
    private TextView details;
    private ImportProgressView progress;
    private TextView progressText;
    private Button primary;
    private Button secondary;
    private View backToGame;

    private volatile boolean cancelled;
    // A download or an install runs: back cancels it instead of leaving.
    private boolean busy;
    private RpuRelease release;
    private Thread worker;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // RPU is a Fallout 2 mod: other games never reach it (the Mods button is hidden).
        if (!GameRoots.isFallout2(this)) {
            finish();
            return;
        }
        setContentView(R.layout.activity_import);
        title = findViewById(R.id.import_title);
        message = findViewById(R.id.import_message);
        details = findViewById(R.id.import_details);
        progress = findViewById(R.id.import_progress);
        progressText = findViewById(R.id.import_progress_text);
        primary = findViewById(R.id.import_primary);
        secondary = findViewById(R.id.import_secondary);
        backToGame = findViewById(R.id.import_back);
        backToGame.setOnClickListener(v -> leave());
        applyFont(findViewById(R.id.import_panel));

        query();
    }

    @Override
    public void onBackPressed() {
        if (busy) {
            cancelled = true;
            return;
        }
        leave();
    }

    @Override
    protected void onDestroy() {
        cancelled = true;
        super.onDestroy();
    }

    private void leave() {
        cancelled = true;
        startActivity(new Intent(this, MainActivity.class));
        finish();
    }

    private void applyFont(View view) {
        Typeface font;
        try {
            font = Typeface.createFromAsset(getAssets(), "ce.dat/fonts/PTM55FT.ttf");
        } catch (RuntimeException e) {
            font = Typeface.MONOSPACE;
        }
        applyFont(view, font);
    }

    private void applyFont(View view, Typeface font) {
        if (view instanceof TextView) {
            ((TextView) view).setTypeface(font);
        } else if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int index = 0; index < group.getChildCount(); index++) applyFont(group.getChildAt(index), font);
        }
    }

    private void show(String text, String detailsText, String primaryText, View.OnClickListener onPrimary,
        String secondaryText, View.OnClickListener onSecondary) {
        title.setText(R.string.rpu_title);
        backToGame.setVisibility(busy ? View.GONE : View.VISIBLE);
        message.setText(text);
        details.setText(detailsText);
        details.setVisibility(detailsText != null ? View.VISIBLE : View.GONE);
        progress.setVisibility(View.GONE);
        progressText.setVisibility(View.GONE);
        primary.setText(primaryText);
        primary.setOnClickListener(onPrimary);
        primary.setVisibility(primaryText != null ? View.VISIBLE : View.GONE);
        secondary.setText(secondaryText);
        secondary.setOnClickListener(onSecondary);
        secondary.setVisibility(secondaryText != null ? View.VISIBLE : View.GONE);
        LinearLayout.LayoutParams params = (LinearLayout.LayoutParams) secondary.getLayoutParams();
        params.setMarginEnd(primaryText != null ? Math.round(10 * getResources().getDisplayMetrics().density) : 0);
        secondary.setLayoutParams(params);
    }

    private void showError(String text) {
        busy = false;
        show(text, null, getString(R.string.rpu_retry), v -> query(), getString(R.string.import_cancel), v -> leave());
    }

    // Asks GitHub for the latest release.
    private void query() {
        busy = true;
        cancelled = false;
        show(getString(R.string.rpu_checking), null, null, null, getString(R.string.import_cancel), v -> cancelled = true);
        progress.setVisibility(View.VISIBLE);
        progress.setIndeterminate(true);
        startWorker(() -> {
            try {
                String json = httpText(RpuRelease.API_URL);
                RpuRelease found = RpuRelease.parse(json);
                runOnUiThread(() -> {
                    busy = false;
                    if (cancelled) { leave(); return; }
                    if (found == null) showError(getString(R.string.rpu_no_zip));
                    else { release = found; showInfo(); }
                });
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "release query failed", e);
                runOnUiThread(() -> {
                    if (cancelled) leave();
                    else showError(getString(R.string.rpu_offline, String.valueOf(e.getMessage())));
                });
            }
        });
    }

    private void showInfo() {
        busy = false;
        long size = RpuRelease.toMegabytes(release.zip.size);
        String text = getString(R.string.rpu_info, release.modName(), size);
        if (isVanilla()) text += getString(R.string.rpu_info_vanilla);
        show(text, null, getString(R.string.rpu_download), v -> beforeDownload(), getString(R.string.import_cancel), v -> leave());
    }

    private boolean isVanilla() {
        File base = GameRoots.activeGameRoot(this);
        if (base == null) return true;
        try (InputStream in = new FileInputStream(new File(base, "profiles/active.txt"))) {
            String active = new String(readAll(in), StandardCharsets.UTF_8).trim();
            return active.isEmpty() || active.equals("Vanilla");
        } catch (IOException e) {
            return true;
        }
    }

    private boolean onUnmetered() {
        ConnectivityManager manager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        if (manager == null) return false;
        Network network = manager.getActiveNetwork();
        NetworkCapabilities caps = network == null ? null : manager.getNetworkCapabilities(network);
        if (caps == null) return false;
        boolean wifi = caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
            || caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET);
        return !RpuRelease.warnNotWifi(wifi, caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED));
    }

    private void beforeDownload() {
        File base = GameRoots.activeGameRoot(this);
        if (base == null) {
            showError(getString(R.string.rpu_download_failed, "no storage"));
            return;
        }
        // Same file already downloaded or on its way: don't fetch it twice.
        if (alreadyQueued()) {
            openDownloads(getString(R.string.rpu_bg_exists));
            return;
        }
        if (!RpuRelease.enoughSpace(base.getUsableSpace(), release.zip.size, 0)) {
            busy = false;
            show(getString(R.string.rpu_no_space, RpuRelease.toMegabytes(RpuRelease.neededBytes(release.zip.size, 0))), null,
                getString(R.string.rpu_retry), v -> beforeDownload(), getString(R.string.import_cancel), v -> leave());
            return;
        }
        if (!onUnmetered()) {
            show(getString(R.string.rpu_no_wifi, RpuRelease.toMegabytes(release.zip.size)), null,
                getString(R.string.rpu_continue), v -> enqueue(false), getString(R.string.import_cancel), v -> showInfo());
            return;
        }
        enqueue(true);
    }

    private boolean alreadyQueued() {
        try {
            for (DownloadIndex.Item item : DownloadIndex.read(ModDownloads.folder(this))) {
                if (DownloadIndex.RPU.equals(item.source) && release.modName().equals(item.title)
                    && !item.installed && !DownloadIndex.FAILED_HINT.equals(item.status)) {
                    return true;
                }
            }
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "index read failed", e);
        }
        return false;
    }

    private void enqueue(boolean wifiOnly) {
        long id = ModDownloads.enqueue(this, release.zip.url, release.zipName(), release.modName(), null,
            "Fallout2CE-Android", null, wifiOnly, DownloadIndex.RPU);
        if (id < 0) {
            showError(getString(R.string.rpu_download_failed, getString(R.string.dl_start_failed)));
            return;
        }
        openDownloads(getString(R.string.rpu_bg_started));
    }

    private void openDownloads(String text) {
        Toast.makeText(this, text, Toast.LENGTH_LONG).show();
        startActivity(new Intent(this, DownloadsActivity.class));
        finish();
    }

    private void startWorker(Runnable job) {
        worker = new Thread(job);
        worker.start();
    }

    private String httpText(String url) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(15000);
        connection.setReadTimeout(30000);
        connection.setRequestProperty("User-Agent", "Fallout2CE-Android");
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        try {
            int status = connection.getResponseCode();
            if (status != 200) throw new IOException("HTTP " + status);
            try (InputStream in = connection.getInputStream()) {
                return new String(readAll(in), StandardCharsets.UTF_8);
            }
        } finally {
            connection.disconnect();
        }
    }

    private static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int count;
        while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
        return out.toByteArray();
    }
}
