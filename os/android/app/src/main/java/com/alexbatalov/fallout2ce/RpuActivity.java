package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.List;

// "Install RPU" (Mods screen footer): downloads the Restoration Project
// release zip from GitHub (never bundled in the app), resumable, then installs
// it through ModInstall like a zip chosen by hand. Same panel as ImportActivity.
// Back / Cancel work at every step; a stopped download keeps its .part file.
public class RpuActivity extends Activity {
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
    private File folder;
    private Thread worker;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
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

        File base = getExternalFilesDir(null);
        folder = base == null ? null : new File(base, "rpu-download");
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
        // Leaving keeps the partial file; stop the thread.
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

    private long partLength() {
        if (folder == null) return 0;
        return new File(folder, release.partName()).length();
    }

    private void showInfo() {
        busy = false;
        long have = new File(folder, release.zipName()).exists() ? release.zip.size : partLength();
        long size = RpuRelease.toMegabytes(release.zip.size);
        String text = getString(R.string.rpu_info, release.modName(), size);
        if (isVanilla()) text += getString(R.string.rpu_info_vanilla);
        String go = have > 0 && have < release.zip.size
            ? getString(R.string.rpu_resume, have / 1048576, size) : getString(R.string.rpu_download);
        show(text, null, go, v -> beforeDownload(), getString(R.string.import_cancel), v -> leave());
    }

    private boolean isVanilla() {
        File base = getExternalFilesDir(null);
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
        if (folder == null) {
            showError(getString(R.string.rpu_download_failed, "no storage"));
            return;
        }
        boolean zipReady = new File(folder, release.zipName()).exists();
        if (!zipReady) {
            long needed = RpuRelease.neededBytes(release.zip.size, partLength());
            File base = getExternalFilesDir(null);
            if (base != null && !RpuRelease.enoughSpace(base.getUsableSpace(), release.zip.size, partLength())) {
                busy = false;
                show(getString(R.string.rpu_no_space, RpuRelease.toMegabytes(needed)), null,
                    getString(R.string.rpu_retry), v -> beforeDownload(), getString(R.string.import_cancel), v -> leave());
                return;
            }
            if (!onUnmetered()) {
                show(getString(R.string.rpu_no_wifi, RpuRelease.toMegabytes(release.zip.size - partLength())), null,
                    getString(R.string.rpu_continue), v -> download(), getString(R.string.import_cancel), v -> showInfo());
                return;
            }
        }
        download();
    }

    private void download() {
        busy = true;
        cancelled = false;
        show(getString(R.string.rpu_downloading, 0, RpuRelease.toMegabytes(release.zip.size)), null, null, null,
            getString(R.string.import_cancel), v -> cancelled = true);
        progress.setVisibility(View.VISIBLE);
        progress.setIndeterminate(true);
        startWorker(() -> {
            File zip = new File(folder, release.zipName());
            try {
                if (!zip.exists()) fetch(zip);
                if (cancelled) {
                    runOnUiThread(() -> { busy = false; showStopped(); });
                    return;
                }
                runOnUiThread(() -> {
                    show(getString(R.string.mod_installing), null, null, null, getString(R.string.import_cancel), v -> cancelled = true);
                    progress.setVisibility(View.VISIBLE);
                    progress.setIndeterminate(true);
                });
                install(zip);
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "rpu failed", e);
                runOnUiThread(() -> showError(getString(R.string.rpu_download_failed, String.valueOf(e.getMessage()))));
            }
        });
    }

    private void showStopped() {
        show(getString(R.string.rpu_paused), null, getString(R.string.rpu_retry), v -> showInfo(),
            getString(R.string.import_cancel), v -> leave());
    }

    // Downloads (resuming a .part file) into [zip]; returns early when
    // cancelled, leaving the .part file for the next time.
    private void fetch(File zip) throws IOException {
        if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("can't make " + folder);
        // Parts of other releases are no use.
        File[] old = folder.listFiles();
        if (old != null) {
            for (File file : old) {
                if (!file.getName().equals(release.partName()) && !file.getName().equals(release.zipName())) {
                    file.delete();
                }
            }
        }
        File part = new File(folder, release.partName());
        long total = release.zip.size;
        long have = part.length();
        if (have > total) { part.delete(); have = 0; }
        if (RpuRelease.resume(have, total) != RpuRelease.Resume.DONE) {
            HttpURLConnection connection = open(release.zip.url);
            try {
                String range = RpuRelease.rangeHeader(have, total);
                if (range != null) connection.setRequestProperty("Range", range);
                int status = connection.getResponseCode();
                if (status == 416) {
                    // The partial file isn't ours: start again.
                    part.delete();
                    have = 0;
                    connection.disconnect();
                    connection = open(release.zip.url);
                    status = connection.getResponseCode();
                    range = null;
                }
                if (status != 200 && status != 206) throw new IOException("HTTP " + status);
                if (range != null && !RpuRelease.rangeAccepted(status, connection.getHeaderField("Content-Range"), have, total)) {
                    if (status == 206) throw new IOException("unexpected range");
                    // The server ignored the Range header and sends all of it.
                    have = 0;
                } else if (range == null && status == 206) {
                    throw new IOException("unexpected range");
                }
                try (InputStream in = connection.getInputStream(); RandomAccessFile out = new RandomAccessFile(part, "rw")) {
                    out.setLength(have);
                    out.seek(have);
                    byte[] buffer = new byte[65536];
                    long lastUpdate = 0;
                    int count;
                    while (!cancelled && (count = in.read(buffer)) != -1) {
                        out.write(buffer, 0, count);
                        have += count;
                        long now = System.currentTimeMillis();
                        if (now - lastUpdate > 250) {
                            lastUpdate = now;
                            showProgress(have, total);
                        }
                    }
                    if (cancelled) return;
                }
            } finally {
                connection.disconnect();
            }
        }
        if (part.length() != total) throw new IOException("incomplete download (" + part.length() + " of " + total + ")");
        if (!part.renameTo(zip)) throw new IOException("can't finish " + zip);
    }

    private void showProgress(long done, long total) {
        runOnUiThread(() -> {
            message.setText(getString(R.string.rpu_downloading, done / 1048576, RpuRelease.toMegabytes(total)));
            progress.setIndeterminate(false);
            progress.setFraction(total > 0 ? (float) done / total : 0);
        });
    }

    private void install(File zip) throws IOException {
        if (!waitForGameToClose()) {
            runOnUiThread(() -> showError(getString(R.string.import_game_running)));
            return;
        }
        try {
            ModInstall.Result result = ModInstall.install(this, Uri.fromFile(zip), release.modName(),
                getExternalFilesDir(null), new ModInstall.Progress() {
                    @Override
                    public void update(long done, long total, String path) {
                        runOnUiThread(() -> {
                            progress.setIndeterminate(false);
                            progress.setFraction(total > 0 ? (float) done / total : 0);
                        });
                    }

                    @Override
                    public boolean isCancelled() {
                        return cancelled;
                    }
                });
            zip.delete();
            runOnUiThread(() -> {
                busy = false;
                String skipped = result.skipped.isEmpty() ? null
                    : getString(R.string.mod_skipped, String.join("\n", result.skipped));
                show(getString(R.string.rpu_installed, result.id), skipped,
                    getString(R.string.import_back_to_game), v -> leave(), null, null);
            });
        } catch (GameImport.CancelledException e) {
            // The zip stays: installing it again needs no new download.
            runOnUiThread(() -> { busy = false; showInfo(); });
        } catch (NativeArchive.Failure e) {
            zip.delete();
            runOnUiThread(() -> showError(getString(R.string.rpu_bad_zip)));
        } catch (ModInstall.Problem e) {
            String text = e.message == R.string.mod_no_space ? getString(e.message, e.neededMb) : getString(e.message);
            runOnUiThread(() -> showError(text));
        }
    }

    private void startWorker(Runnable job) {
        worker = new Thread(job);
        worker.start();
    }

    private HttpURLConnection open(String url) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(20000);
        connection.setReadTimeout(30000);
        connection.setInstanceFollowRedirects(true);
        connection.setRequestProperty("User-Agent", "Fallout2CE-Android");
        connection.setRequestProperty("Accept", "application/octet-stream, application/vnd.github+json");
        return connection;
    }

    private String httpText(String url) throws IOException {
        HttpURLConnection connection = open(url);
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

    // The game (the app's main process) exits when this opens; its files are
    // changed only once it has.
    private boolean waitForGameToClose() {
        ActivityManager manager = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        for (int attempt = 0; attempt < 150; attempt++) {
            boolean running = false;
            List<ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
            if (processes != null) {
                for (ActivityManager.RunningAppProcessInfo process : processes) {
                    running |= process.processName.equals(getPackageName());
                }
            }
            if (!running) return true;
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                return false;
            }
        }
        return false;
    }
}
