package com.alexbatalov.fallout2ce;

import android.util.Log;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

// Et Tu runs on Fallout 2's master.dat and critter.dat (the top root). Fallout
// 1's content comes loose: the files its undat_files.txt lists are unpacked
// from the imported Fallout 1 master.dat (DAT1, which the engine cannot open)
// into "<top>/games/ettu/data" (the game's master_patches/critter_patches).
// A marker records which master.dat and list were unpacked, so this runs once.
final class EtTuUndat {
    private static final String TAG = "EtTuUndat";
    static final String LIST = "undat_files.txt";
    static final String MARKER = ".undat-done";

    private EtTuUndat() {
    }

    // The extraction step; injectable so tests need no DAT1 files.
    interface Extractor {
        boolean isDat1(File dat) throws IOException;

        Dat1Extractor.Result extract(File dat, List<String> entries, File outDir, boolean lowerCase) throws IOException;
    }

    static final Extractor REAL = new Extractor() {
        @Override
        public boolean isDat1(File dat) throws IOException {
            return Dat1Extractor.isDat1(dat);
        }

        @Override
        public Dat1Extractor.Result extract(File dat, List<String> entries, File outDir, boolean lowerCase) throws IOException {
            return Dat1Extractor.extract(dat, entries, outDir, lowerCase);
        }
    };

    enum Outcome { NOT_APPLICABLE, UP_TO_DATE, UNPACKED }

    static File gameDir(File top) {
        return new File(new File(top, "games"), "ettu");
    }

    static File dataDir(File top) {
        return new File(gameDir(top), "data");
    }

    private static File list(File top) {
        return new File(gameDir(top), LIST);
    }

    private static File marker(File top) {
        return new File(gameDir(top), MARKER);
    }

    private static File master(File top) {
        return new File(Fallout1Import.baseDir(top), "master.dat");
    }

    // Both inputs are there (Et Tu installed, Fallout 1 imported).
    static boolean applicable(File top) {
        return top != null && list(top).isFile() && master(top).isFile();
    }

    private static String stamp(File top) {
        File master = master(top);
        return "master.size=" + master.length() + "\nmaster.mtime=" + master.lastModified()
            + "\nlist.size=" + list(top).length() + "\n";
    }

    // Applicable and no fresh marker: the unpack is still to be done.
    static boolean needed(File top) {
        if (!applicable(top)) {
            return false;
        }
        try {
            String done = new String(Files.readAllBytes(marker(top).toPath()), StandardCharsets.UTF_8);
            return !done.equals(stamp(top));
        } catch (IOException e) {
            return true;
        }
    }

    // Unpacks when needed. Blocking: never call on the UI thread. On failure an
    // IOException is thrown and no marker is written (the next call retries).
    static synchronized Outcome run(File top, Extractor extractor) throws IOException {
        if (!applicable(top)) {
            return Outcome.NOT_APPLICABLE;
        }
        if (!needed(top)) {
            return Outcome.UP_TO_DATE;
        }
        File master = master(top);
        if (!extractor.isDat1(master)) {
            throw new IOException("Fallout 1 master.dat is not a Fallout 1 archive");
        }
        List<String> entries = new ArrayList<>();
        for (String line : Files.readAllLines(list(top).toPath(), StandardCharsets.UTF_8)) {
            String entry = line.trim();
            if (!entry.isEmpty()) {
                entries.add(entry);
            }
        }
        if (entries.isEmpty()) {
            throw new IOException(LIST + " is empty");
        }
        File out = dataDir(top);
        if (!out.isDirectory() && !out.mkdirs()) {
            throw new IOException("can't make " + out);
        }
        // Lower case: the engine finds loose files case-insensitively (src/platform_compat.cc,
        // compat_resolve_path), and the mod's own loose files are lower case.
        Dat1Extractor.Result result = extractor.extract(master, entries, out, true);
        if (result.written <= 0) {
            throw new IOException("nothing was unpacked from Fallout 1's master.dat");
        }
        if (!result.missing.isEmpty()) {
            log(result.missing.size() + " listed files are not in master.dat, e.g. " + result.missing.get(0));
        }
        Files.write(marker(top).toPath(), stamp(top).getBytes(StandardCharsets.UTF_8));
        log("unpacked " + result.written + " files");
        return Outcome.UNPACKED;
    }

    private static void log(String message) {
        try {
            Log.w(TAG, message);
        } catch (RuntimeException notAndroid) {
            // JVM unit tests: android.util.Log is not available.
        }
    }

    // User-facing text for a failure.
    static String failureText(IOException e) {
        return "Could not unpack the Fallout 1 data for Et Tu: " + e.getMessage();
    }
}
