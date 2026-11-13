package com.alexbatalov.fallout2ce;

import android.app.DownloadManager;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

// Background mod downloads through Android's DownloadManager: they go on
// while the game is played or the app is closed, show progress in the
// notification bar and resume after a network loss. Files land in
// <active game root>/mod-downloads, indexed by DownloadIndex.
//
// Entry point for any downloader (Nexus browser, RPU):
//   long id = ModDownloads.enqueue(context, url, "name.zip", "Mod title",
//       cookies, userAgent, referer, wifiOnly);   // -1 when it could not start
final class ModDownloads {
    private static final String TAG = "ModDownloads";
    static final String FOLDER = "mod-downloads";

    private ModDownloads() { }

    static File folder(Context context) {
        File base = GameRoots.activeGameRoot(context);
        File folder = new File(base, FOLDER);
        folder.mkdirs();
        return folder;
    }

    static long enqueue(Context context, String url, String fileName, String title, String cookies,
                        String userAgent, String referer, boolean wifiOnly) {
        return enqueue(context, url, fileName, title, cookies, userAgent, referer, wifiOnly, DownloadIndex.OTHER);
    }

    // [source]: DownloadIndex.NEXUS / RPU / OTHER. [cookies], [userAgent] and
    // [referer] may be null. Returns the DownloadManager id, or -1.
    static long enqueue(Context context, String url, String fileName, String title, String cookies,
                        String userAgent, String referer, boolean wifiOnly, String source) {
        return enqueue(context, folder(context.getApplicationContext()), url, fileName, title, cookies, userAgent,
            referer, wifiOnly, source);
    }

    // The game installers' fixed folder: games install no matter which game is active.
    static File gameFolder(Context context) {
        File folder = GameInstall.downloadsFolder(GameRoots.topRoot(context));
        folder.mkdirs();
        return folder;
    }

    // The file lands in exactly [folder]/<unique name>: index, uniqueness check
    // and DownloadManager destination all use the same folder.
    static long enqueue(Context context, File folder, String url, String fileName, String title, String cookies,
                        String userAgent, String referer, boolean wifiOnly, String source) {
        try {
            Context app = context.getApplicationContext();
            folder.mkdirs();
            String file = DownloadIndex.uniqueName(folder, fileName);
            String shown = title == null || title.trim().isEmpty() ? file : title.trim();

            DownloadManager.Request request = new DownloadManager.Request(Uri.parse(url));
            request.setTitle(shown);
            request.setDescription(file);
            request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            request.setAllowedOverMetered(!wifiOnly);
            request.setAllowedOverRoaming(!wifiOnly);
            request.setDestinationUri(Uri.fromFile(DownloadIndex.destination(folder, file)));
            if (cookies != null && !cookies.isEmpty()) request.addRequestHeader("Cookie", cookies);
            if (userAgent != null && !userAgent.isEmpty()) request.addRequestHeader("User-Agent", userAgent);
            if (referer != null && !referer.isEmpty()) request.addRequestHeader("Referer", referer);
            final DownloadManager manager = manager(app);
            final long id = manager.enqueue(request);
            DownloadIndex.update(folder, items -> {
                items.removeIf(i -> i.file.equals(file));
                items.add(new DownloadIndex.Item(id, file, shown, source, DownloadIndex.QUEUED, false));
            });
            return id;
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "enqueue failed", e);
            return -1;
        }
    }

    private static DownloadManager manager(Context context) {
        return (DownloadManager) context.getSystemService(Context.DOWNLOAD_SERVICE);
    }

    // The manager's rows for the indexed ids.
    static Map<Long, DownloadIndex.Live> query(Context context, List<DownloadIndex.Item> items) {
        Map<Long, DownloadIndex.Live> live = new HashMap<>();
        List<Long> ids = new ArrayList<>();
        for (DownloadIndex.Item i : items) if (i.id >= 0) ids.add(i.id);
        if (ids.isEmpty()) return live;
        long[] array = new long[ids.size()];
        for (int n = 0; n < array.length; n++) array[n] = ids.get(n);
        try (Cursor c = manager(context).query(new DownloadManager.Query().setFilterById(array))) {
            if (c == null) return live;
            int idCol = c.getColumnIndexOrThrow(DownloadManager.COLUMN_ID);
            int statusCol = c.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS);
            int reasonCol = c.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON);
            int doneCol = c.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR);
            int totalCol = c.getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES);
            while (c.moveToNext()) {
                live.put(c.getLong(idCol), new DownloadIndex.Live(c.getInt(statusCol), c.getInt(reasonCol),
                    c.getLong(doneCol), c.getLong(totalCol)));
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "query failed", e);
        }
        return live;
    }

    // Current rows for the screen; also stores finished/failed states in the
    // index so they survive the manager forgetting the id.
    static List<DownloadIndex.Row> rows(Context context) {
        return rows(context, folder(context));
    }

    static List<DownloadIndex.Row> rows(Context context, File folder) {
        List<DownloadIndex.Item> items;
        try {
            items = DownloadIndex.read(folder);
        } catch (IOException e) {
            items = new ArrayList<>();
        }
        Map<Long, DownloadIndex.Live> live = query(context, items);
        List<DownloadIndex.Row> rows = DownloadIndex.rows(folder, items, live);
        boolean changed = false;
        for (DownloadIndex.Row r : rows) {
            String wanted = r.state.kind == DownloadIndex.Kind.FAILED ? DownloadIndex.FAILED_HINT
                : r.state.active() ? DownloadIndex.QUEUED : DownloadIndex.DONE;
            if (r.indexed && !wanted.equals(r.item.status)) changed = true;
        }
        if (changed) {
            final Map<Long, DownloadIndex.Live> snapshot = live;
            try {
                DownloadIndex.update(folder, current -> {
                    for (DownloadIndex.Item i : current) {
                        DownloadIndex.Live l = snapshot.get(i.id);
                        if (l == null) continue;
                        DownloadIndex.State s = DownloadIndex.map(l, i.installed);
                        i.status = s.kind == DownloadIndex.Kind.FAILED ? DownloadIndex.FAILED_HINT
                            : s.active() ? DownloadIndex.QUEUED : DownloadIndex.DONE;
                    }
                });
            } catch (IOException e) {
                Log.w(TAG, "index update failed", e);
            }
        }
        return rows;
    }

    static int readyCount(Context context) {
        try {
            return DownloadIndex.readyCount(rows(context));
        } catch (RuntimeException e) {
            return 0;
        }
    }

    // Stops a download (and removes its partial file) or deletes a finished one.
    static void remove(Context context, DownloadIndex.Row row) {
        File folder = folder(context);
        if (row.item.id >= 0) {
            try {
                manager(context).remove(row.item.id);
            } catch (RuntimeException e) {
                Log.w(TAG, "remove failed", e);
            }
        }
        new File(folder, row.item.file).delete();
        try {
            final String file = row.item.file;
            DownloadIndex.update(folder, items -> items.removeIf(i -> i.file.equals(file)));
        } catch (IOException e) {
            Log.w(TAG, "index update failed", e);
        }
    }

    static void markInstalled(Context context, String file) {
        try {
            DownloadIndex.update(folder(context), items -> {
                DownloadIndex.Item i = DownloadIndex.find(items, file);
                if (i != null) i.installed = true;
            });
        } catch (IOException e) {
            Log.w(TAG, "index update failed", e);
        }
    }
}
