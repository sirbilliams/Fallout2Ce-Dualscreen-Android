package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

// "Add Fallout 1 files": the player's own Fallout 1 (a folder or an archive,
// the same sources as the Fallout 2 import) copied to the shared Fallout 1
// base, "<files>/base/fallout1", which Et Tu and the other Fallout 1 based
// games read (game_catalog.cc, gameBasePath). Only what a Fallout 1 base
// needs comes: master.dat, critter.dat and data/ (music, sound...). Fallout 2
// at the top of the files folder is never touched; the copy goes through
// GameFiles' staging and journal, so a failed or cancelled import leaves the
// base as it was.
final class Fallout1Import {
    static final String BASE = "base/fallout1";

    private Fallout1Import() {
    }

    // The base folder for the app's files folder [top].
    static File baseDir(File top) {
        return new File(top, BASE);
    }

    // The base holds a Fallout 1 and no replacement is half done.
    static boolean isInstalled(File top) {
        File base = baseDir(top);
        return new File(base, "master.dat").isFile()
            && new File(base, "critter.dat").isFile()
            && !GameFiles.isCommitPending(base);
    }

    // The chosen files aren't Fallout 1's (Fallout 2's, or another game).
    static final class NotFallout1Exception extends IOException {
        final boolean fallout2;

        NotFallout1Exception(boolean fallout2, String detail) {
            super("not Fallout 1: " + detail);
            this.fallout2 = fallout2;
        }
    }

    enum DatKind { FALLOUT1, FALLOUT2, UNKNOWN }

    // Fallout 2's DAT ends with its tree size and its own file size (little
    // endian); Fallout 1's starts with the directory count (big endian) and
    // has no such end.
    static DatKind datKind(File file) throws IOException {
        long length = file.length();
        if (length < 16) {
            return DatKind.UNKNOWN;
        }
        try (RandomAccessFile dat = new RandomAccessFile(file, "r")) {
            dat.seek(length - 8);
            long treeSize = littleEndian(dat);
            long dataSize = littleEndian(dat);
            if (dataSize == length && treeSize > 0 && treeSize < length) {
                return DatKind.FALLOUT2;
            }
            dat.seek(0);
            long directories = dat.readInt() & 0xFFFFFFFFL;
            return directories > 0 && directories <= 65535 ? DatKind.FALLOUT1 : DatKind.UNKNOWN;
        }
    }

    private static long littleEndian(RandomAccessFile file) throws IOException {
        long value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            value |= (long) file.read() << shift;
        }
        return value;
    }

    // The path (below the game's folder) a Fallout 1 base keeps, else null:
    // master.dat and critter.dat (named in lower case: the files folder is
    // case sensitive) and data/ but for saves.
    static String destination(String path) {
        String lower = path.toLowerCase(Locale.ROOT);
        if (lower.equals("master.dat") || lower.equals("critter.dat")) {
            return lower;
        }
        if (lower.startsWith("data/") && !lower.startsWith("data/savegame/")) {
            return "data/" + path.substring(5);
        }
        return null;
    }

    // Null without a Fallout 1 (no master.dat and critter.dat). The Fallout 2
    // refused: its patch000.dat is next to its master.dat. Only the files of
    // a base are in `files`.
    static GameImport.Found find(GameImport.Source source) throws IOException {
        GameImport.Found found = GameImport.find(source, false);
        if (found == null) {
            return null;
        }
        List<GameImport.Entry> all = new ArrayList<>(found.files);
        found.files.clear();
        found.size = 0;
        found.mods.clear();
        found.saves = 0;
        for (GameImport.Entry entry : all) {
            String path = entry.path.substring(found.root.length());
            if (path.equalsIgnoreCase("patch000.dat")) {
                throw new NotFallout1Exception(true, "patch000.dat");
            }
            String destination = destination(path);
            if (destination != null) {
                found.files.add(entry);
                found.destinations.put(entry, destination);
                found.size += entry.size;
            }
        }
        return found;
    }

    static GameFiles.Plan plan(GameImport.Found found, File top) {
        List<String> paths = new ArrayList<>();
        List<Long> sizes = new ArrayList<>();
        for (GameImport.Entry entry : found.files) {
            paths.add(found.destinations.get(entry));
            sizes.add(entry.size);
        }
        File base = baseDir(top);
        GameFiles.Plan plan = GameFiles.plan(paths, sizes, base, GameFiles.installedGameFiles(base), false);
        // Nothing of an earlier base is deleted; same names are replaced.
        plan.removed.clear();
        return plan;
    }

    // Space the copy needs (the files go next to the base first), plus some.
    static long neededBytes(GameFiles.Plan plan) {
        return plan.bytesToWrite + 64L * 1024 * 1024;
    }

    // Copies [found] into the base of [top] as [plan] says. The staged
    // master.dat and critter.dat are checked to be Fallout 1's before they
    // are moved in; else nothing changes and NotFallout1Exception is thrown.
    static void install(GameImport.Source source, GameImport.Found found, GameFiles.Plan plan, File top, GameImport.Progress progress) throws IOException {
        File base = baseDir(top);
        if (!base.isDirectory() && !base.mkdirs()) {
            throw new IOException("can't make " + base);
        }
        GameFiles.finish(base);

        Map<String, String> installed = GameFiles.installedGameFiles(base);
        Map<GameImport.Entry, String> pathOf = new HashMap<>();
        for (GameImport.Entry entry : found.files) {
            pathOf.put(entry, found.destinations.get(entry));
        }

        File staging = new File(base, GameFiles.STAGING);
        GameFiles.StagingSink sink = new GameFiles.StagingSink(base, installed, pathOf, plan.bytesToRead, (done, total, path) -> {
            if (progress.isCancelled()) {
                throw new GameImport.CancelledException();
            }
            progress.update(done, total, path);
        });
        try {
            source.read(found.files, sink);
            for (String name : new String[] { "master.dat", "critter.dat" }) {
                // Not staged: the same as the installed one (compared while copying).
                String own = installed.get(name);
                File file = sink.staged.contains(name) ? new File(staging, name) : new File(base, own != null ? own : name);
                DatKind kind = file.isFile() ? datKind(file) : DatKind.UNKNOWN;
                if (kind != DatKind.FALLOUT1) {
                    throw new NotFallout1Exception(kind == DatKind.FALLOUT2, name);
                }
            }
        } catch (IOException | RuntimeException e) {
            sink.abandon();
            GameFiles.deleteRecursively(staging);
            throw e;
        }
        GameFiles.commit(base, sink.staged, plan.removed, installed);
    }
}
