package com.alexbatalov.fallout2ce;

import android.app.ActivityManager;
import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

// One-click Dustlight HD: a finished download that is an HD art part (by
// name or content) is installed at once through HdPackInstall, no confirm.
// Called by the download receiver and by the Nexus guide banner (so a part
// that finished while the game ran is picked up later). Same process as the
// Nexus browser (:import). Results are toasts; the banner shows the state.
final class DustlightAuto {
    private static final String TAG = "DustlightAuto";
    private static final Object LOCK = new Object();
    // file -> tier of a part that is not the installed tier (waiting for an answer)
    private static final Map<String, Integer> blocked = new ConcurrentHashMap<>();
    private static final Set<String> kept = ConcurrentHashMap.newKeySet();      // answered "keep"
    private static final Set<String> failed = ConcurrentHashMap.newKeySet();    // file|size that did not install
    private static final Map<String, Boolean> verdicts = new ConcurrentHashMap<>();  // file|size -> HD content
    private static final Set<String> engineSeen = ConcurrentHashMap.newKeySet();
    private static final Set<String> installedHere = ConcurrentHashMap.newKeySet();
    private static volatile String working;
    private static volatile int workingPercent;

    private DustlightAuto() { }

    static Object lock() {
        return LOCK;
    }

    // --- what the banner shows ---

    static final class Item {
        final String file;
        final DustlightHd.Name name;
        final DownloadIndex.Kind kind;
        final int percent;
        final boolean blocked;
        final boolean failed;

        Item(String file, DustlightHd.Name name, DownloadIndex.Kind kind, int percent, boolean blocked, boolean failed) {
            this.file = file;
            this.name = name;
            this.kind = kind;
            this.percent = percent;
            this.blocked = blocked;
            this.failed = failed;
        }
    }

    static final class Report {
        DustlightHd.Status status;
        final List<Item> items = new ArrayList<>();
        String working;
        int workingPercent;
        int blockedTier;       // 0 = none; else the tier waiting for "switch?"
        int deletableCount;
        long deletableBytes;
    }

    static Report report(Context context) {
        Report report = new Report();
        File game = context.getExternalFilesDir(null);
        report.status = game == null ? new DustlightHd.Status(0, 0, -1, new ArrayList<>()) : DustlightHd.status(game);
        report.working = working;
        report.workingPercent = workingPercent;
        for (DownloadIndex.Row row : ModDownloads.rows(context)) {
            DustlightHd.Name name = DustlightHd.classify(row.item.file);
            boolean hd = name.kind == DustlightHd.Kind.ENGINE || isHdRow(context, row);
            if (!hd) continue;
            Item item = new Item(row.item.file, name, row.state.kind, row.state.percent,
                blocked.containsKey(row.item.file), failed.contains(key(row)));
            report.items.add(item);
            if (item.blocked) report.blockedTier = blocked.get(row.item.file);
            if (name.kind == DustlightHd.Kind.ART && row.item.installed && new File(ModDownloads.folder(context), row.item.file).isFile()) {
                report.deletableCount++;
                report.deletableBytes += row.size;
            }
        }
        return report;
    }

    // --- install ---

    // Installs every finished HD part that waits. Safe to call often and from
    // any thread; does nothing while the game runs (it owns the files then).
    static void processPending(Context context) {
        synchronized (LOCK) {
            File game = context.getExternalFilesDir(null);
            if (game == null || gameRunning(context)) return;
            for (DownloadIndex.Row row : ModDownloads.rows(context)) {
                if (!ready(context, row)) continue;
                DustlightHd.Name name = DustlightHd.classify(row.item.file);
                if (name.kind == DustlightHd.Kind.ENGINE) {
                    if (engineSeen.add(row.item.file)) toast(context, context.getString(R.string.dl_hd_engine, row.item.file));
                    continue;
                }
                if (!isHdRow(context, row)) continue;
                String file = row.item.file;
                if (blocked.containsKey(file) || kept.contains(file) || failed.contains(key(row))) continue;
                installOne(context, game, row, false);
            }
        }
    }

    // The player agreed to change tier: installs the parts that waited for it.
    static void switchTier(Context context, int tier) {
        synchronized (LOCK) {
            File game = context.getExternalFilesDir(null);
            if (game == null || gameRunning(context)) return;
            for (DownloadIndex.Row row : ModDownloads.rows(context)) {
                Integer waiting = blocked.get(row.item.file);
                if (waiting == null || waiting != tier || !ready(context, row)) continue;
                installOne(context, game, row, true);
            }
        }
    }

    // The player kept the installed tier: the waiting parts are left alone.
    static void keepTier() {
        kept.addAll(blocked.keySet());
        blocked.clear();
    }

    private static void installOne(Context context, File game, DownloadIndex.Row row, boolean allowSwitch) {
        String file = row.item.file;
        DustlightHd.Status before = DustlightHd.status(game);
        working = file;
        workingPercent = 0;
        String message;
        try {
            HdPackInstall.Result r = HdPackInstall.install(context, Uri.fromFile(new File(ModDownloads.folder(context), file)),
                file, game, new HdPackInstall.Progress() {
                    public void update(long done, long total, String path) {
                        workingPercent = total <= 0 ? 0 : (int) Math.min(100, done * 100 / total);
                    }
                    public boolean isCancelled() { return false; }
                }, allowSwitch);
            ModDownloads.markInstalled(context, file);
            installedHere.add(file);
            blocked.remove(file);
            boolean ready = DustlightHd.enableIfNewlyComplete(game, before);
            message = ready ? context.getString(R.string.dl_hd_ready)
                : context.getString(R.string.dl_hd_part_done, file, r.tier, r.packs);
        } catch (HdPackInstall.TierMismatch m) {
            blocked.put(file, m.incoming);
            message = context.getString(R.string.dl_hd_tier_ask, m.incoming, m.installed);
        } catch (HdPackInstall.Problem p) {
            failed.add(key(row));
            message = context.getString(R.string.nexus_install_failed, file,
                p.message == R.string.mod_no_space ? context.getString(p.message, p.neededMb) : context.getString(p.message));
        } catch (HdPackInstall.Invalid e) {
            failed.add(key(row));
            message = context.getString(R.string.nexus_install_failed, file, context.getString(R.string.hd_invalid, e.getMessage()));
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "install failed", e);
            failed.add(key(row));
            message = context.getString(R.string.nexus_install_failed, file,
                context.getString(R.string.import_failed, String.valueOf(e.getMessage())));
        } finally {
            working = null;
        }
        toast(context, message);
    }

    // Deletes the installed HD zips; only ever called after the player tapped
    // "Delete" on the offer.
    static void deleteInstalledZips(Context context) {
        for (DownloadIndex.Row row : ModDownloads.rows(context)) {
            if (DustlightHd.classify(row.item.file).kind == DustlightHd.Kind.ART && row.item.installed) {
                ModDownloads.remove(context, row);
            }
        }
    }

    // --- helpers ---

    private static boolean ready(Context context, DownloadIndex.Row row) {
        return row.state.kind == DownloadIndex.Kind.DOWNLOADED && !row.item.installed
            && !DownloadIndex.RPU.equals(row.item.source)
            && row.item.file.toLowerCase(java.util.Locale.ROOT).endsWith(".zip");
    }

    private static String key(DownloadIndex.Row row) {
        return row.item.file + "|" + row.size;
    }

    // An HD art part: by name (while it downloads and once installed) and,
    // for a finished unknown zip, by content (cached).
    private static boolean isHdRow(Context context, DownloadIndex.Row row) {
        DustlightHd.Name name = DustlightHd.classify(row.item.file);
        if (name.kind == DustlightHd.Kind.ENGINE) return false;
        boolean finished = row.state.kind == DownloadIndex.Kind.DOWNLOADED;
        if (row.item.installed) return name.kind == DustlightHd.Kind.ART || installedHere.contains(row.item.file);
        if (!finished) return name.kind == DustlightHd.Kind.ART;
        if (!row.item.file.toLowerCase(java.util.Locale.ROOT).endsWith(".zip") || DownloadIndex.RPU.equals(row.item.source)) return false;
        Boolean cached = verdicts.get(key(row));
        if (cached != null) return cached;
        boolean hd;
        try (GameImport.Source source = new GameImport.ArchiveSource(context.getContentResolver(),
            Uri.fromFile(new File(ModDownloads.folder(context), row.item.file)), (done, total) -> { })) {
            hd = DustlightHd.isHdContent(source.list());
        } catch (IOException | RuntimeException e) {
            hd = false;
        }
        verdicts.put(key(row), hd);
        return hd;
    }

    private static boolean gameRunning(Context context) {
        ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> processes = manager == null ? null : manager.getRunningAppProcesses();
        if (processes == null) return false;
        for (ActivityManager.RunningAppProcessInfo p : processes) {
            if (p.processName.equals(context.getPackageName())) return true;
        }
        return false;
    }

    private static void toast(Context context, String text) {
        final Context app = context.getApplicationContext();
        new Handler(Looper.getMainLooper()).post(() -> Toast.makeText(app, text, Toast.LENGTH_LONG).show());
    }

    // A manual install of this zip failed before: let the auto path try again.
    static void forget(String file) {
        failed.removeIf(k -> k.startsWith(file + "|"));
        kept.remove(file);
        blocked.remove(file);
    }

    static Set<String> blockedFiles() {
        return new HashSet<>(blocked.keySet());
    }
}
