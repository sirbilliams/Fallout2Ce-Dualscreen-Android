package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.ConnectivityManager;
import android.net.NetworkCapabilities;
import android.util.Log;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.concurrent.*;

/** Downloaded picker artwork. Installers call afterInstall only after committing game files. */
public final class GamePictures {
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
    private static final java.util.Set<String> pending = ConcurrentHashMap.newKeySet();
    private static final java.util.Map<String, Long> attempted = new ConcurrentHashMap<>();

    // Creator and picture source per installable game; mirrors gameCatalog() in
    // src/game_catalog.cc (the native picker passes the same strings to prepareGamePictures).
    static String[] catalogInfo(String id) {
        if ("ettu".equals(id)) return new String[] { "rotators", "https://github.com/rotators/Fo1in2" };
        if ("sonora".equals(id)) return new String[] { "Nevada Band / Cambragol / NovaRain", "https://github.com/NovaRain/Fallout-Sonora-English" };
        if ("resurrection".equals(id)) return new String[] { "Resurrection Team", "https://resurrection.cz/en/download/" };
        return null;
    }

    /** Looks up creator and source from the catalog; never throws. */
    public static void afterInstall(Context context, String id) {
        try {
            String[] info = catalogInfo(id);
            if (info != null) afterInstall(context, id, info[0], info[1]);
        } catch (RuntimeException e) {
            Log.d("GamePictures", "afterInstall skipped for " + id + ": " + e.getClass().getSimpleName());
        }
    }

    public static void afterInstall(Context context, String id, String creator, String source) {
        request(context, id, creator, source, true);
    }

    static void request(Context context, String id, String creator, String source, boolean installed) {
        File top = context.getExternalFilesDir(null);
        if (top == null) return;
        File dir;
        try { dir = PickerPictures.directory(top, id, installed); }
        catch (IllegalArgumentException e) { return; }
        PickerPictures.purgeSocialCard(dir);
        // Installed games take their art from their own files; art fetched online for
        // an installed game earlier is replaced when the game's files give a picture.
        if (installed && !"fallout2".equals(id)) {
            if (GameArt.isOwn(dir)) return;
            String key = dir.getAbsolutePath() + "#own";
            long now = android.os.SystemClock.elapsedRealtime();
            if (now - attempted.getOrDefault(key, -60000L) < 60000 || !pending.add(key)) return;
            attempted.put(key, now);
            WORKER.execute(() -> {
                try { GameArt.generate(top, id, dir, creator, source); }
                catch (IOException | RuntimeException e) {
                    Log.d("GamePictures", "No game-file art for " + id + ": " + e.getClass().getSimpleName());
                } finally { pending.remove(key); }
            });
            return;
        }
        if (new File(dir, "art.rgba").isFile()) return;
        String key = dir.getAbsolutePath();
        long now = android.os.SystemClock.elapsedRealtime();
        if (now - attempted.getOrDefault(key, -60000L) < 60000 || !pending.add(key)) return;
        Context app = context.getApplicationContext();
        WORKER.execute(() -> {
            try {
                ConnectivityManager cm = (ConnectivityManager) app.getSystemService(Context.CONNECTIVITY_SERVICE);
                NetworkCapabilities caps = cm == null ? null : cm.getNetworkCapabilities(cm.getActiveNetwork());
                boolean wifi = caps != null && (caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                        || caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET));
                if (!PickerPictures.mayFetch(caps != null && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET),
                        wifi, cm == null || cm.isActiveNetworkMetered())) return;
                attempted.put(key, android.os.SystemClock.elapsedRealtime());
                long deadline = android.os.SystemClock.elapsedRealtime() + 25000;
                // Online art is only for not-installed previews: the best large landscape image
                // of the project page / README (no badges, logos, icons or GitHub social cards).
                java.util.List<String> candidates = new java.util.ArrayList<>();
                String api = PickerPictures.readmeApi(source);
                try {
                    if (api == null) {
                        candidates.addAll(PickerPictures.imageUrls(source, PickerPictures.utf8(fetch(source, PickerPictures.PAGE_CAP, deadline)), 6));
                    } else {
                        String raw = PickerPictures.readmeUrl(PickerPictures.utf8(fetch(api, PickerPictures.PAGE_CAP, deadline)));
                        if (raw != null) candidates.addAll(PickerPictures.imageUrls(raw, PickerPictures.utf8(fetch(raw, PickerPictures.PAGE_CAP, deadline)), 6));
                    }
                } catch (IOException ignored) { /* No page art. */ }
                String image = null;
                byte[] data = null;
                BitmapFactory.Options bounds = new BitmapFactory.Options();
                long bestScore = -1;
                for (String candidate : candidates) {
                    try {
                        byte[] bytes = fetch(candidate, installed ? PickerPictures.FULL_CAP : PickerPictures.PREVIEW_CAP, deadline);
                        BitmapFactory.Options probe = new BitmapFactory.Options();
                        probe.inJustDecodeBounds = true;
                        BitmapFactory.decodeByteArray(bytes, 0, bytes.length, probe);
                        if (probe.outWidth > 16384 || probe.outHeight > 16384 || (long) probe.outWidth * probe.outHeight > 64000000) continue;
                        if (!"image/png".equals(probe.outMimeType) && !"image/jpeg".equals(probe.outMimeType)) continue;
                        long score = PickerPictures.imageScore(probe.outWidth, probe.outHeight);
                        if (score > bestScore) { bestScore = score; image = candidate; data = bytes; bounds = probe; }
                    } catch (IOException ignored) { /* skip this candidate */ }
                }
                if (image == null || data == null) return;
                int edge = installed ? 1920 : 480;
                BitmapFactory.Options opts = new BitmapFactory.Options();
                opts.inSampleSize = 1;
                while (bounds.outWidth / opts.inSampleSize > edge || bounds.outHeight / opts.inSampleSize > edge) opts.inSampleSize *= 2;
                Bitmap bitmap = BitmapFactory.decodeByteArray(data, 0, data.length, opts);
                if (bitmap == null) return;
                try {
                    if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("picture directory");
                    File temp = new File(dir, "art.rgba.part");
                    try (DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(temp)))) {
                        // PRAW + big-endian dimensions + straight RGBA bytes (no Android pixel layout assumptions).
                        out.writeInt(0x50524157); out.writeInt(bitmap.getWidth()); out.writeInt(bitmap.getHeight());
                        int[] row = new int[bitmap.getWidth()];
                        for (int y = 0; y < bitmap.getHeight(); ++y) {
                            bitmap.getPixels(row, 0, row.length, 0, y, row.length, 1);
                            for (int pixel : row) {
                                out.writeByte(pixel >> 16); out.writeByte(pixel >> 8); out.writeByte(pixel); out.writeByte(pixel >>> 24);
                            }
                        }
                    }
                    // Publish credit before art: an installed image always has its attribution.
                    File credit = new File(dir, "credit.txt.part");
                    Files.write(credit.toPath(), ("by " + creator + " - " + source + "\nImage: " + image + "\n").getBytes(StandardCharsets.UTF_8));
                    Files.move(credit.toPath(), new File(dir, "credit.txt").toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
                    Files.move(temp.toPath(), new File(dir, "art.rgba").toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
                } finally { bitmap.recycle(); }
            } catch (IOException | RuntimeException e) {
                Log.d("GamePictures", "No picture for " + id + ": " + e.getClass().getSimpleName());
            } finally { pending.remove(key); }
        });
    }

    private static byte[] fetch(String address, int cap, long deadline) throws IOException {
        for (int redirects = 0; redirects < 5; ++redirects) {
            if (PickerPictures.safeUrl(address, address) == null) throw new IOException("unsafe picture URL");
            int remaining = (int) Math.min(6000, deadline - android.os.SystemClock.elapsedRealtime());
            if (remaining <= 0) throw new IOException("picture deadline");
            HttpURLConnection connection = (HttpURLConnection) new URL(address).openConnection();
            try {
                connection.setConnectTimeout(remaining); connection.setReadTimeout(remaining);
                connection.setInstanceFollowRedirects(false);
                connection.setRequestProperty("User-Agent", "Fallout2CE-Picker");
                int status = connection.getResponseCode();
                if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                    String location = connection.getHeaderField("Location");
                    if (location == null) throw new IOException("missing redirect");
                    address = PickerPictures.safeUrl(address, location);
                    if (address == null) throw new IOException("unsafe redirect");
                    continue;
                }
                if (status != 200 || connection.getContentLengthLong() > cap) throw new IOException("picture status/size");
                try (InputStream in = connection.getInputStream()) {
                    // Check the wall-clock budget on each chunk, including slow trickle responses.
                    InputStream timed = new FilterInputStream(in) {
                        @Override public int read(byte[] b, int off, int len) throws IOException {
                            if (android.os.SystemClock.elapsedRealtime() > deadline) throw new IOException("picture deadline");
                            return super.read(b, off, len);
                        }
                    };
                    return PickerPictures.readLimited(timed, cap);
                }
            } finally { connection.disconnect(); }
        }
        throw new IOException("picture redirects");
    }
}
