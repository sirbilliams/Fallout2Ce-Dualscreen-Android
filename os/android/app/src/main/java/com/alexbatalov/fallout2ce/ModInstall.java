package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

// ZIP-only mod import. Originals and saves are never installation targets.
final class ModInstall {
    interface Progress extends GameImport.Progress { }

    static final class Result {
        final String id;
        final List<String> skipped;

        Result(String id, List<String> skipped) {
            this.id = id;
            this.skipped = new ArrayList<>(skipped);
        }
    }

    static final class Problem extends IOException {
        final int message;
        final long neededMb;

        Problem(int message) {
            this(message, 0);
        }

        Problem(int message, long neededMb) {
            this.message = message;
            this.neededMb = neededMb;
        }
    }

    // Mirror modListUniqueId: ASCII lowercase, one '-' per disallowed
    // character, preserve dots/underscores/hyphens, suffix starting at -2.
    static String uniqueId(Iterable<String> existing, String wanted) {
        StringBuilder text = new StringBuilder();
        for (byte value : wanted.getBytes(StandardCharsets.UTF_8)) {
            int ch = value & 255;
            if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
            boolean allowed = ch >= 'a' && ch <= 'z' || ch >= '0' && ch <= '9'
                || ch == '.' || ch == '_' || ch == '-';
            text.append(allowed ? (char) ch : '-');
        }
        String base = text.toString();
        Set<String> ids = new HashSet<>();
        for (String id : existing) ids.add(id);
        String id = base;
        for (int suffix = 2; ids.contains(id); suffix++) id = base + "-" + suffix;
        return id;
    }

    static String detectRoot(List<GameImport.Entry> entries) throws IOException {
        Set<String> roots = new HashSet<>();
        for (GameImport.Entry entry : entries) {
            String path = entry.path;
            // Validate before stripping a wrapper folder, including on JVM.
            if (path.startsWith("/") || path.contains("\\") || path.contains(":")) {
                throw new IOException("bad archive path: " + path);
            }
            for (String part : path.split("/", -1)) {
                if (part.isEmpty() || part.equals(".") || part.equals("..") || part.indexOf('\0') != -1
                    || part.indexOf('\n') != -1 || part.indexOf('\r') != -1 || part.indexOf('\t') != -1) {
                    throw new IOException("bad archive path: " + path);
                }
            }
            String lower = path.toLowerCase(Locale.ROOT);
            String[] parts = path.split("/");
            int offset = 0;
            for (int index = 0; index < parts.length - 1; index++) {
                if (parts[index].equalsIgnoreCase("data") || parts[index].equalsIgnoreCase("mods")) {
                    roots.add(path.substring(0, offset));
                    break;
                }
                offset += parts[index].length() + 1;
            }
            if (lower.endsWith(".dat") && !parts[parts.length - 1].equalsIgnoreCase("ce.dat") && !lower.startsWith("__macosx/")) {
                roots.add(path.substring(0, path.lastIndexOf('/') + 1));
            }
        }
        int depth = Integer.MAX_VALUE;
        List<String> shallowest = new ArrayList<>();
        for (String root : roots) {
            int candidate = root.split("/", -1).length - 1;
            if (candidate < depth) { depth = candidate; shallowest.clear(); }
            if (candidate == depth) shallowest.add(root);
        }
        if (shallowest.size() > 1) throw new GameImport.SeveralGamesException(shallowest);
        return shallowest.isEmpty() ? null : shallowest.get(0);
    }

    static Result install(Context context, Uri zip, File gameDir, Progress progress) throws IOException {
        return install(context, zip, null, gameDir, progress);
    }

    // [wantedName]: the mod's name (its id is made from it), e.g. the file name
    // of a download in the app's own folder; null = the Uri's display name.
    static Result install(Context context, Uri zip, String wantedName, File gameDir, Progress progress) throws IOException {
        String name = wantedName != null ? wantedName : "mod.zip";
        if (wantedName == null) try (Cursor cursor = context.getContentResolver().query(zip,
            new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) name = cursor.getString(0);
        }
        // NativeArchive also supports RAR/7z, but the mod picker promises ZIP.
        try (InputStream in = context.getContentResolver().openInputStream(zip)) {
            if (in == null || in.read() != 'P' || in.read() != 'K') throw new Problem(R.string.mod_unreadable);
            int third = in.read();
            int fourth = in.read();
            if (third == 5 && fourth == 6) throw new Problem(R.string.mod_no_files);
            if (third != 3 || fourth != 4) throw new Problem(R.string.mod_unreadable);
        }
        try (GameImport.Source source = new GameImport.ArchiveSource(context.getContentResolver(), zip, (done, total) -> {
            if (progress.isCancelled()) throw new GameImport.CancelledException();
            progress.update(done, total, "");
        })) {
            return install(source, name, gameDir, progress);
        } catch (NativeArchive.Failure failure) {
            throw new Problem(R.string.mod_unreadable);
        }
    }

    // Pure-file entry point for tests; source still reads via GameImport's sink.
    static Result install(GameImport.Source source, String name, File gameDir, Progress progress) throws IOException {
        if (gameDir == null) throw new IOException("game storage unavailable");
        cleanup(gameDir);
        GameImport.Found found = GameImport.findMod(source);
        if (found == null || found.files.isEmpty()) throw new Problem(R.string.mod_no_files);
        long unpacked = 0;
        for (GameImport.Entry entry : source.list()) {
            if (entry.size < 0 || entry.size > Long.MAX_VALUE / 2 - unpacked) throw new IOException("invalid archive size");
            unpacked += entry.size;
        }
        long needed = unpacked * 2;
        if (gameDir.getUsableSpace() < needed) {
            throw new Problem(R.string.mod_no_space, needed / 1048576 + (needed % 1048576 == 0 ? 0 : 1));
        }
        String active = readUtf8(new File(gameDir, "profiles/active.txt")).trim();
        boolean vanilla = active.isEmpty() || active.equals("Vanilla");
        if (!vanilla && (active.equals(".") || active.equals("..") || active.contains("/") || active.contains("\\")
            || active.contains("\n") || active.contains("\r") || active.contains("\t"))) {
            throw new IOException("invalid active profile");
        }
        File store = new File(gameDir, "modstore");
        mkdir(store);
        if (name.toLowerCase(Locale.ROOT).endsWith(".zip")) name = name.substring(0, name.length() - 4);
        String base = uniqueId(Collections.emptyList(), name);
        if (base.isEmpty() || base.equals(".") || base.equals("..") || base.startsWith(".tmp-")) base = "mod-" + base;
        String[] installed = store.list();
        if (installed == null) throw new IOException("can't list mod store");
        String id = uniqueId(Arrays.asList(installed), base);
        File temp = GameImport.targetFile(store, ".tmp-" + id);
        if (temp.exists()) throw new IOException("can't clean interrupted mod install");
        try {
            GameFiles.Plan plan = GameImport.plan(found, temp, false);
            if (progress.isCancelled()) throw new GameImport.CancelledException();
            GameImport.install(source, found, plan, false, temp, progress);
            if (progress.isCancelled()) throw new GameImport.CancelledException();
            if (vanilla) {
                for (GameImport.Entry entry : found.files) {
                    String path = found.destinations.get(entry);
                    if (path.indexOf('/') != -1 || !path.endsWith(".dat")) found.skipped.add(path);
                }
                installVanilla(temp, gameDir, id);
            } else {
                File destination = GameImport.targetFile(store, id);
                if (destination.exists() || !temp.renameTo(destination)) throw new IOException("can't publish mod " + id);
                File list = GameImport.targetFile(gameDir, "profiles/" + active + "/modlist.txt");
                appendAtomic(list, Collections.singletonList("+" + id));
            }
            return new Result(id, found.skipped);
        } finally {
            GameFiles.deleteRecursively(temp);
        }
    }

    private static void installVanilla(File temp, File gameDir, String id) throws IOException {
        File mods = new File(gameDir, "mods");
        mkdir(mods);
        GameFiles.finish(mods);
        File[] dats = temp.listFiles(file -> file.isFile() && file.getName().endsWith(".dat"));
        if (dats == null || dats.length == 0) throw new Problem(R.string.mod_vanilla_dat_only);
        Arrays.sort(dats, (a, b) -> a.getName().compareTo(b.getName()));
        Set<String> names = new HashSet<>();
        for (String existing : mods.list()) names.add(existing.toLowerCase(Locale.ROOT));
        List<String> added = new ArrayList<>();
        File staging = new File(mods, GameFiles.STAGING);
        mkdir(staging);
        try {
            for (File dat : dats) {
                String fileName = id + ".dat";
                for (int suffix = 2; names.contains(fileName); suffix++) fileName = id + "-" + suffix + ".dat";
                names.add(fileName);
                if (!dat.renameTo(GameImport.targetFile(staging, fileName))) throw new IOException("can't stage " + fileName);
                added.add(fileName);
            }
            String order = appendLines(readUtf8(new File(mods, "mods_order.txt")), added);
            writeUtf8(new File(staging, "mods_order.txt"), order);
            added.add("mods_order.txt");
            GameFiles.commit(mods, added, Collections.emptyList(), GameFiles.installedGameFiles(mods));
        } finally {
            // Keep staging if its journal needs to finish on next launch.
            if (!new File(mods, GameFiles.JOURNAL).exists()) GameFiles.deleteRecursively(staging);
        }
    }

    static void cleanup(File gameDir) throws IOException {
        if (gameDir == null) return;
        File mods = new File(gameDir, "mods");
        if (mods.isDirectory()) GameFiles.finish(mods);
        File[] leftovers = new File(gameDir, "modstore").listFiles(file -> file.getName().startsWith(".tmp-"));
        if (leftovers != null) for (File file : leftovers) GameFiles.deleteRecursively(file);
    }

    private static String appendLines(String original, List<String> lines) {
        StringBuilder text = new StringBuilder(original);
        if (text.length() > 0 && text.charAt(text.length() - 1) != '\n') text.append('\n');
        for (String line : lines) text.append(line).append('\n');
        return text.toString();
    }

    private static void appendAtomic(File list, List<String> lines) throws IOException {
        mkdir(list.getParentFile());
        File temp = new File(list.getParentFile(), ".modlist.tmp");
        writeUtf8(temp, appendLines(readUtf8(list), lines));
        if (!temp.renameTo(list)) throw new IOException("can't write " + list);
    }

    private static String readUtf8(File file) throws IOException {
        if (!file.exists()) return "";
        try (InputStream in = new FileInputStream(file)) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int count;
            while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    private static void writeUtf8(File file, String text) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
    }

    private static void mkdir(File folder) throws IOException {
        if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("can't make " + folder);
    }
}
