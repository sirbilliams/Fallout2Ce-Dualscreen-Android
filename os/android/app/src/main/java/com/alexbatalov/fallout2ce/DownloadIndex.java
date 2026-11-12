package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

// Pure logic of the mod downloads (no Android classes, JVM-testable): the
// index file in the downloads folder, the mapping of DownloadManager columns
// to a state, the list of rows (indexed downloads plus any other archive in
// the folder) and the space they use.
final class DownloadIndex {
    static final String INDEX = "downloads.tsv";
    static final String LOCK = ".downloads.lock";

    static final String NEXUS = "nexus";
    static final String RPU = "rpu";
    static final String OTHER = "other";

    // Stored hint; the live DownloadManager state wins while it knows the id.
    static final String QUEUED = "queued";
    static final String DONE = "done";
    static final String FAILED_HINT = "failed";

    // android.app.DownloadManager constants (STATUS_*, PAUSED_*).
    static final int DM_PENDING = 1;
    static final int DM_RUNNING = 2;
    static final int DM_PAUSED = 4;
    static final int DM_SUCCESSFUL = 8;
    static final int DM_FAILED = 16;
    static final int PAUSED_WAITING_TO_RETRY = 1;
    static final int PAUSED_WAITING_FOR_NETWORK = 2;
    static final int PAUSED_QUEUED_FOR_WIFI = 3;
    static final int ERROR_INSUFFICIENT_SPACE = 1006;

    static final class Item {
        long id;         // DownloadManager id, -1 when unknown (migrated / manual)
        String file;     // name inside the downloads folder
        String title;    // mod name
        String source;   // nexus / rpu / other
        String status;   // QUEUED / DONE / FAILED_HINT
        boolean installed;

        Item(long id, String file, String title, String source, String status, boolean installed) {
            this.id = id;
            this.file = file;
            this.title = title;
            this.source = source;
            this.status = status;
            this.installed = installed;
        }
    }

    // The DownloadManager's row for an id (columns status, reason, bytes).
    static final class Live {
        final int status;
        final int reason;
        final long done;
        final long total;

        Live(int status, int reason, long done, long total) {
            this.status = status;
            this.reason = reason;
            this.done = done;
            this.total = total;
        }
    }

    enum Kind { DOWNLOADING, WAITING_NETWORK, WAITING_WIFI, DOWNLOADED, INSTALLED, FAILED }

    static final class State {
        final Kind kind;
        final int percent;  // DOWNLOADING: 0..100, -1 when the size is unknown
        final int reason;   // FAILED: DownloadManager error / HTTP code, 0 unknown

        State(Kind kind, int percent, int reason) {
            this.kind = kind;
            this.percent = percent;
            this.reason = reason;
        }

        boolean active() {
            return kind == Kind.DOWNLOADING || kind == Kind.WAITING_NETWORK || kind == Kind.WAITING_WIFI;
        }
    }

    static final class Row {
        final Item item;   // never null; a synthetic one for files not in the index
        final boolean indexed;
        final long size;   // bytes on disk (or downloaded so far)
        final long modified;
        final State state;

        Row(Item item, boolean indexed, long size, long modified, State state) {
            this.item = item;
            this.indexed = indexed;
            this.size = size;
            this.modified = modified;
            this.state = state;
        }
    }

    static boolean isArchiveName(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.endsWith(".zip") || lower.endsWith(".7z") || lower.endsWith(".rar");
    }

    // --- DownloadManager columns -> state ---

    static State map(Live live, boolean installed) {
        switch (live.status) {
            case DM_SUCCESSFUL:
                return new State(installed ? Kind.INSTALLED : Kind.DOWNLOADED, 100, 0);
            case DM_FAILED:
                return new State(Kind.FAILED, 0, live.reason);
            case DM_PAUSED:
                if (live.reason == PAUSED_QUEUED_FOR_WIFI) return new State(Kind.WAITING_WIFI, percent(live), 0);
                return new State(Kind.WAITING_NETWORK, percent(live), 0);
            case DM_PENDING:
            case DM_RUNNING:
            default:
                return new State(Kind.DOWNLOADING, percent(live), 0);
        }
    }

    static int percent(Live live) {
        if (live.total <= 0) return live.done > 0 ? -1 : 0;
        long p = live.done * 100 / live.total;
        return (int) Math.max(0, Math.min(100, p));
    }

    // State of a row; [live] is null when the DownloadManager does not know the id.
    static State state(Item item, Live live, boolean fileExists) {
        if (live != null) return map(live, item.installed);
        if (!fileExists) return new State(Kind.FAILED, 0, 0);
        if (QUEUED.equals(item.status)) return new State(Kind.FAILED, 0, 0);  // removed from the manager half way
        if (FAILED_HINT.equals(item.status)) return new State(Kind.FAILED, 0, 0);
        return new State(item.installed ? Kind.INSTALLED : Kind.DOWNLOADED, 100, 0);
    }

    // --- rows ---

    // Indexed downloads plus every other archive in [folder]. Index entries
    // whose file is gone and that the manager no longer knows are left out.
    // Active downloads first, then newest first.
    static List<Row> rows(File folder, List<Item> index, Map<Long, Live> live) {
        List<Row> rows = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        for (Item item : index) {
            File file = new File(folder, item.file);
            Live l = live.get(item.id);
            boolean exists = file.isFile();
            if (!exists && l == null) continue;
            if (!seen.add(item.file)) continue;
            State state = state(item, l, exists);
            long size = exists ? file.length() : (l != null ? l.done : 0);
            rows.add(new Row(item, true, size, exists ? file.lastModified() : Long.MAX_VALUE, state));
        }
        File[] files = folder.listFiles();
        if (files != null) {
            for (File file : files) {
                if (!file.isFile() || !isArchiveName(file.getName()) || seen.contains(file.getName())) continue;
                String name = file.getName();
                int dot = name.lastIndexOf('.');
                Item item = new Item(-1, name, dot > 0 ? name.substring(0, dot) : name, OTHER, DONE, false);
                rows.add(new Row(item, false, file.length(), file.lastModified(), new State(Kind.DOWNLOADED, 100, 0)));
            }
        }
        Collections.sort(rows, (a, b) -> {
            if (a.state.active() != b.state.active()) return a.state.active() ? -1 : 1;
            return Long.compare(b.modified, a.modified);
        });
        return rows;
    }

    static long totalSize(List<Row> rows) {
        long total = 0;
        for (Row row : rows) total += row.size;
        return total;
    }

    // Downloaded and not installed yet.
    static int readyCount(List<Row> rows) {
        int count = 0;
        for (Row row : rows) if (row.state.kind == Kind.DOWNLOADED) count++;
        return count;
    }

    // "1.5 GB", "320 MB", "12 KB".
    static String formatSize(long bytes) {
        if (bytes >= 1L << 30) return String.format(Locale.ROOT, "%.1f GB", bytes / (double) (1L << 30));
        if (bytes >= 1L << 20) return String.format(Locale.ROOT, "%d MB", bytes >> 20);
        if (bytes >= 1L << 10) return String.format(Locale.ROOT, "%d KB", bytes >> 10);
        return bytes + " B";
    }

    // --- index file ---
    // New: id TAB file TAB title TAB source TAB status TAB installed(0/1).
    // Old (Nexus step 4): file TAB title; read as a finished download.

    static List<Item> parse(List<String> lines) {
        List<Item> items = new ArrayList<>();
        for (String line : lines) {
            String[] f = line.split("\t", -1);
            Item item = null;
            if (f.length >= 6 && isNumber(f[0])) {
                item = new Item(Long.parseLong(f[0]), NexusDownloads.sanitize(f[1]), f[2], f[3].isEmpty() ? OTHER : f[3],
                    f[4].isEmpty() ? DONE : f[4], "1".equals(f[5]));
            } else if (f.length >= 2 && !f[0].isEmpty()) {
                String file = NexusDownloads.sanitize(f[0]);
                item = new Item(-1, file, f[1], NEXUS, DONE, false);
            }
            if (item != null) items.add(item);
        }
        return items;
    }

    private static boolean isNumber(String s) {
        if (s.isEmpty() || s.length() > 18) return false;
        int start = s.charAt(0) == '-' ? 1 : 0;
        if (start == s.length()) return false;
        for (int i = start; i < s.length(); i++) if (!Character.isDigit(s.charAt(i))) return false;
        return true;
    }

    static String format(List<Item> items) {
        StringBuilder text = new StringBuilder();
        for (Item i : items) {
            text.append(i.id).append('\t').append(clean(i.file)).append('\t').append(clean(i.title)).append('\t')
                .append(clean(i.source)).append('\t').append(clean(i.status)).append('\t').append(i.installed ? "1" : "0").append('\n');
        }
        return text.toString();
    }

    private static String clean(String s) {
        return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
    }

    static List<Item> read(File folder) throws IOException {
        File index = new File(folder, INDEX);
        if (!index.isFile()) return new ArrayList<>();
        return parse(Files.readAllLines(index.toPath(), StandardCharsets.UTF_8));
    }

    static void write(File folder, List<Item> items) throws IOException {
        File temp = new File(folder, INDEX + ".tmp");
        Files.write(temp.toPath(), format(items).getBytes(StandardCharsets.UTF_8));
        Files.move(temp.toPath(), new File(folder, INDEX).toPath(), StandardCopyOption.REPLACE_EXISTING);
    }

    interface Change {
        void apply(List<Item> items);
    }

    // Read-modify-write under a file lock (the game and import processes
    // and the broadcast receiver all touch the index).
    static void update(File folder, Change change) throws IOException {
        if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("no downloads folder");
        synchronized (DownloadIndex.class) {
            try (RandomAccessFile raf = new RandomAccessFile(new File(folder, LOCK), "rw");
                 FileChannel channel = raf.getChannel();
                 FileLock ignored = channel.lock()) {
                List<Item> items = read(folder);
                change.apply(items);
                write(folder, items);
            }
        }
    }

    static Item find(List<Item> items, String file) {
        for (Item i : items) if (i.file.equals(file)) return i;
        return null;
    }

    static Item findById(List<Item> items, long id) {
        for (Item i : items) if (i.id == id) return i;
        return null;
    }
}
