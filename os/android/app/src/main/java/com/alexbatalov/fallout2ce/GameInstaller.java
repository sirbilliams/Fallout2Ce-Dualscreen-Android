package com.alexbatalov.fallout2ce;

import android.app.AlertDialog;
import android.app.job.*;
import android.content.*;
import android.net.ConnectivityManager;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

final class GameInstaller {
    static final int JOB = 7312;
    private static final Set<String> resolving = new HashSet<>();
    static final String RESURRECTION = "https://bitbucket.org/resurrection/fallout-resurrection/downloads/FoRes_EN_1_5.zip";

    static String game(DownloadIndex.Row row) {
        String source = row.item.source;
        return source.startsWith("game:") && GameInstall.supported(source.substring(5)) ? source.substring(5) : null;
    }

    static void schedule(Context context) {
        JobScheduler jobs = (JobScheduler) context.getSystemService(Context.JOB_SCHEDULER_SERVICE);
        if (jobs.getPendingJob(JOB) == null)
            jobs.schedule(new JobInfo.Builder(JOB, new ComponentName(context, GameInstallJob.class))
                .setMinimumLatency(1000).setBackoffCriteria(30000, JobInfo.BACKOFF_POLICY_LINEAR).build());
    }

    static void status(Context context, String id, String text) throws IOException {
        Files.write(new File(ModDownloads.folder(context), "game-" + id + ".status").toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    static String status(Context context, String id) {
        File top = GameRoots.topRoot(context);
        if (!GameInstall.supported(id)) return "Not installed";
        if (GameInstall.installed(top, id)) return "Installed";
        try {
            for (DownloadIndex.Row row : ModDownloads.rows(context)) {
                if (!id.equals(game(row)) || row.item.installed) continue;
                if (row.state.active()) return row.state.kind == DownloadIndex.Kind.DOWNLOADING
                    ? "Downloading " + (row.state.percent < 0 ? "" : row.state.percent + "%") : "Waiting for network / Wi-Fi";
                if (row.state.kind == DownloadIndex.Kind.FAILED) return "Download failed; tap Install to retry";
                break; // newest completed row; old failures must not hide extraction progress
            }
            File state = new File(ModDownloads.folder(context), "game-" + id + ".status");
            if (state.isFile()) return new String(Files.readAllBytes(state.toPath()), StandardCharsets.UTF_8);
        } catch (IOException | RuntimeException ignored) { }
        return "Not installed";
    }

    private static final Map<String, String> sizes = new HashMap<>();
    private static final Map<String, Long> sizeTried = new HashMap<>();

    /**
     * Download size of the game's official archive ("123 MB"), or "" while it is
     * unknown. The first call starts a background lookup (GitHub release asset
     * size or the archive's Content-Length); failures are retried after a minute.
     */
    static String size(Context context, String id) {
        if (!GameInstall.supported(id)) return "";
        synchronized (sizes) {
            String known = sizes.get(id);
            if (known != null) return known;
            long now = android.os.SystemClock.elapsedRealtime();
            Long tried = sizeTried.get(id);
            if (tried != null && now - tried < 60000) return "";
            sizeTried.put(id, now);
        }
        new Thread(() -> {
            try {
                long bytes;
                if (id.equals("resurrection")) {
                    HttpURLConnection connection = connect(RESURRECTION, "HEAD");
                    try { bytes = connection.getResponseCode() == 200 ? connection.getContentLengthLong() : -1; }
                    finally { connection.disconnect(); }
                } else {
                    bytes = GameInstall.asset(id, read("https://api.github.com/repos/" + GameInstall.repo(id) + "/releases/latest")).size;
                }
                if (bytes > 0) synchronized (sizes) { sizes.put(id, DownloadIndex.formatSize(bytes)); }
            } catch (IOException | RuntimeException ignored) { }
        }, "game-size").start();
        return "";
    }

    static void start(MainActivity activity, String id) {
        if (!GameInstall.supported(id)) return;
        synchronized (resolving) { if (!resolving.add(id)) return; }
        new Thread(() -> {
            boolean handedToUi = false;
            try {
                File top = GameRoots.topRoot(activity);
                if (top == null) throw new IOException("Game storage unavailable");
                GameInstall.requireBase(top, id);
                if (GameInstall.installed(top, id)) return;
                for (DownloadIndex.Row row : ModDownloads.rows(activity)) {
                    if (!id.equals(game(row)) || row.item.installed) continue;
                    if (row.state.active()) return;
                    if (row.state.kind == DownloadIndex.Kind.DOWNLOADED) {
                        status(activity, id, "Waiting to install");
                        schedule(activity);
                        return;
                    }
                }
                status(activity, id, "Looking up official release");
                RpuRelease.Asset asset;
                if (id.equals("resurrection")) {
                    HttpURLConnection connection = connect(RESURRECTION, "HEAD");
                    try {
                        if (connection.getResponseCode() != 200) throw new IOException("Official archive is unavailable");
                        long size = connection.getContentLengthLong();
                        if (size <= 0) throw new IOException("Official server did not provide an archive size");
                        asset = new RpuRelease.Asset("FoRes_EN_1_5.zip", size, RESURRECTION);
                    } finally { connection.disconnect(); }
                } else {
                    asset = GameInstall.asset(id, read("https://api.github.com/repos/" + GameInstall.repo(id) + "/releases/latest"));
                }
                long need = GameInstall.preflight(asset.size);
                if (top.getUsableSpace() < need) throw new IOException("Not enough space: reserve " + DownloadIndex.formatSize(need));
                final RpuRelease.Asset selected = asset;
                handedToUi = true;
                activity.runOnUiThread(() -> {
                    if (activity.isFinishing() || activity.isDestroyed()) {
                        releaseResolution(id);
                        return;
                    }
                    ConnectivityManager network = (ConnectivityManager) activity.getSystemService(Context.CONNECTIVITY_SERVICE);
                    if (network.isActiveNetworkMetered()) {
                        new AlertDialog.Builder(activity).setTitle("Metered network")
                            .setMessage("Download " + DownloadIndex.formatSize(selected.size) + "? Wi-Fi is recommended.")
                            .setPositiveButton("Use this network", (d, w) -> enqueue(activity, id, selected, false))
                            .setNeutralButton("Wait for Wi-Fi", (d, w) -> enqueue(activity, id, selected, true))
                            .setNegativeButton("Cancel", (d, w) -> clearStatus(activity, id))
                            .setOnCancelListener(d -> clearStatus(activity, id)).show();
                    } else enqueue(activity, id, selected, false);
                });
            } catch (IOException | RuntimeException e) {
                String message = e.getMessage() == null ? "Install could not start" : e.getMessage();
                try { status(activity, id, message); } catch (IOException ignored) { }
                activity.runOnUiThread(() -> {
                    if (!activity.isFinishing() && !activity.isDestroyed())
                        new AlertDialog.Builder(activity).setTitle("Game installer").setMessage(message).setPositiveButton("OK", null).show();
                });
            } finally { if (!handedToUi) releaseResolution(id); }
        }, "game-release").start();
    }

    private static void releaseResolution(String id) {
        synchronized (resolving) { resolving.remove(id); }
    }

    private static void clearStatus(Context context, String id) {
        releaseResolution(id);
        try { status(context, id, "Not installed"); } catch (IOException ignored) { }
    }

    private static void enqueue(Context context, String id, RpuRelease.Asset asset, boolean wifi) {
        releaseResolution(id);
        long download = ModDownloads.enqueue(context, asset.url, "game-" + id + "-" + asset.name,
            id + " game installer", null, "Fallout2CE", null, wifi, "game:" + id);
        try { status(context, id, download < 0 ? "Could not start download; tap Install to retry" : "Downloading"); }
        catch (IOException ignored) { }
        if (download >= 0) schedule(context);
    }

    private static HttpURLConnection connect(String url, String method) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(15000);
        connection.setReadTimeout(20000);
        connection.setRequestMethod(method);
        connection.setRequestProperty("User-Agent", "Fallout2CE-game-installer");
        connection.setRequestProperty("Accept", url.startsWith("https://api.github.com/") ? "application/vnd.github+json" : "*/*");
        return connection;
    }

    private static String read(String url) throws IOException {
        HttpURLConnection c = connect(url, "GET");
        try {
            if (c.getResponseCode() != 200) throw new IOException("Release lookup failed (HTTP " + c.getResponseCode() + ")");
            try (InputStream in = c.getInputStream(); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[8192];
                int n;
                while ((n = in.read(buffer)) != -1) {
                    if (out.size() + n > 2097152) throw new IOException("Release response too large");
                    out.write(buffer, 0, n);
                }
                return out.toString("UTF-8");
            }
        } finally { c.disconnect(); }
    }
}
