package com.alexbatalov.fallout2ce;

import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.*;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/**
 * Picker art for an INSTALLED game, taken from the game's own files: its
 * splash screens (art/splash/splash*.rix, 640x480 RIX3 paletted images) or else
 * its main menu background (art/intrface/mainmenu.frm + color.pal). Pure JVM
 * code: a small Fallout 2 DAT reader plus loose "data/" files, so the game
 * does not have to be the one running. Output is the PRAW cache GamePictures
 * writes ('PRAW', big-endian width/height, straight RGBA).
 */
final class GameArt {
    static final String MARKER = "game-file:";
    private static final int MAX_ENTRY = 8 * 1024 * 1024;

    private GameArt() { }

    /** A readable file set: loose roots and DAT2 archives, searched in order. */
    static final class Source {
        final List<File> looseRoots = new ArrayList<>();
        final List<File> dats = new ArrayList<>();
        String origin = "";
        final Map<File, Map<String, long[]>> indexes = new HashMap<>();

        byte[] read(String path) {
            String rel = path.replace('\\', '/').toLowerCase(Locale.ROOT);
            for (File root : looseRoots) {
                File f = ci(root, rel);
                if (f != null && f.isFile() && f.length() <= MAX_ENTRY) {
                    try { origin = f.getName(); return Files.readAllBytes(f.toPath()); } catch (IOException ignored) { }
                }
            }
            for (File dat : dats) {
                try {
                    Map<String, long[]> index = indexes.get(dat);
                    if (index == null) { index = index(dat); indexes.put(dat, index); }
                    byte[] data = readEntry(dat, index, rel);
                    if (data != null) { origin = dat.getName(); return data; }
                } catch (IOException | RuntimeException ignored) { }
            }
            return null;
        }
    }

    // Case-insensitive path below [root] (Android file systems are case sensitive).
    private static File ci(File root, String rel) {
        File cur = root;
        for (String part : rel.split("/")) {
            if (part.isEmpty()) continue;
            File exact = new File(cur, part);
            if (exact.exists()) { cur = exact; continue; }
            String[] names = cur.list();
            if (names == null) return null;
            File found = null;
            for (String n : names) if (n.equalsIgnoreCase(part)) { found = new File(cur, n); break; }
            if (found == null) return null;
            cur = found;
        }
        return cur;
    }

    /** Fallout 2 DAT2 entry [rel] (lower case, '/'), or null when absent or unreadable. */
    static byte[] readDat2(File dat, String rel) throws IOException {
        return readEntry(dat, index(dat), rel);
    }

    /** name (lower case, '/') -> {type, real, packed, offset}; empty when [dat] is not a DAT2. */
    static Map<String, long[]> index(File dat) throws IOException {
        Map<String, long[]> map = new HashMap<>();
        try (RandomAccessFile in = new RandomAccessFile(dat, "r")) {
            long length = in.length();
            if (length < 16) return map;
            byte[] tail = new byte[8];
            in.seek(length - 8);
            in.readFully(tail);
            ByteBuffer t = ByteBuffer.wrap(tail).order(ByteOrder.LITTLE_ENDIAN);
            long treeSize = t.getInt() & 0xFFFFFFFFL, dataSize = t.getInt() & 0xFFFFFFFFL;
            if (dataSize != length || treeSize < 4 || treeSize > length - 8 || treeSize > 64L * 1024 * 1024) return map;
            byte[] tree = new byte[(int) treeSize];
            in.seek(length - 8 - treeSize);
            in.readFully(tree);
            ByteBuffer b = ByteBuffer.wrap(tree).order(ByteOrder.LITTLE_ENDIAN);
            long count = b.getInt() & 0xFFFFFFFFL;
            for (long i = 0; i < count; ++i) {
                if (b.remaining() < 4) break;
                int nameLen = b.getInt();
                if (nameLen < 0 || nameLen > 512 || b.remaining() < nameLen + 13) break;
                byte[] nameBytes = new byte[nameLen];
                b.get(nameBytes);
                long type = b.get(), real = b.getInt() & 0xFFFFFFFFL, packed = b.getInt() & 0xFFFFFFFFL, offset = b.getInt() & 0xFFFFFFFFL;
                if (offset + packed > length - 8) continue;
                map.put(new String(nameBytes, StandardCharsets.ISO_8859_1).replace('\\', '/').toLowerCase(Locale.ROOT),
                    new long[] { type, real, packed, offset });
            }
        }
        return map;
    }

    static byte[] readEntry(File dat, Map<String, long[]> index, String rel) throws IOException {
        long[] e = index.get(rel);
        if (e == null || e[1] > MAX_ENTRY || e[2] > MAX_ENTRY) return null;
        byte[] stored = new byte[(int) e[2]];
        try (RandomAccessFile in = new RandomAccessFile(dat, "r")) {
            in.seek(e[3]);
            in.readFully(stored);
        }
        if (e[0] == 0) return stored;
        byte[] out = new byte[(int) e[1]];
        Inflater inflater = new Inflater();
        try {
            inflater.setInput(stored);
            int done = 0;
            while (done < out.length && !inflater.finished()) {
                int n = inflater.inflate(out, done, out.length - done);
                if (n == 0 && (inflater.needsInput() || inflater.needsDictionary())) break;
                done += n;
            }
            return done == out.length ? out : null;
        } catch (DataFormatException ex) {
            return null;
        } finally { inflater.end(); }
    }

    /** Decodes a RIX3 image (palette 6-bit, as the engine's splash reader) to PRAW, or null. */
    static byte[] rixToPraw(byte[] rix) {
        if (rix == null || rix.length < 10 + 768 + 1 || rix[0] != 'R' || rix[1] != 'I' || rix[2] != 'X' || rix[3] != '3') return null;
        int w = (rix[4] & 0xFF) | (rix[5] & 0xFF) << 8, h = (rix[6] & 0xFF) | (rix[7] & 0xFF) << 8;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || rix.length < 10 + 768 + (long) w * h) return null;
        int[] palette = new int[256];
        for (int i = 0; i < 256; ++i) {
            int r = rix[10 + i * 3] & 63, g = rix[11 + i * 3] & 63, bl = rix[12 + i * 3] & 63;
            palette[i] = (r * 255 / 63) << 16 | (g * 255 / 63) << 8 | (bl * 255 / 63);
        }
        // The engine swaps colours 0 and 255 for splash files: index 255 draws black.
        palette[255] = 0;
        return praw(w, h, (x, y) -> 0xFF000000 | palette[rix[10 + 768 + y * w + x] & 0xFF]);
    }

    /** Decodes frame 0 / direction 0 of an FRM with a 6-bit 256-colour palette (index 0 transparent), or null. */
    static byte[] frmToPraw(byte[] frm, byte[] pal) {
        if (frm == null || pal == null || pal.length < 768 || frm.length < 62 + 12) return null;
        ByteBuffer b = ByteBuffer.wrap(frm).order(ByteOrder.BIG_ENDIAN);
        int w = b.getShort(62) & 0xFFFF, h = b.getShort(64) & 0xFFFF;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || frm.length < 62 + 12 + (long) w * h) return null;
        int[] palette = new int[256];
        for (int i = 0; i < 256; ++i)
            palette[i] = (i == 0 ? 0 : 0xFF000000) | ((pal[i * 3] & 63) * 255 / 63) << 16
                | ((pal[i * 3 + 1] & 63) * 255 / 63) << 8 | ((pal[i * 3 + 2] & 63) * 255 / 63);
        return praw(w, h, (x, y) -> palette[frm[62 + 12 + y * w + x] & 0xFF]);
    }

    private interface Pixel { int argb(int x, int y); }

    private static byte[] praw(int w, int h, Pixel pixel) {
        ByteBuffer out = ByteBuffer.allocate(12 + w * h * 4).order(ByteOrder.BIG_ENDIAN);
        out.putInt(0x50524157).putInt(w).putInt(h);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            int p = pixel.argb(x, y);
            out.put((byte) (p >> 16)).put((byte) (p >> 8)).put((byte) p).put((byte) (p >>> 24));
        }
        return out.array();
    }

    /** Size in pixels of a PRAW image (0 when not PRAW). */
    static long area(byte[] praw) {
        if (praw == null || praw.length < 12) return 0;
        ByteBuffer b = ByteBuffer.wrap(praw);
        return b.getInt(0) == 0x50524157 ? (long) b.getInt(4) * b.getInt(8) : 0;
    }

    /**
     * The best picture the game's files give: the largest splash (first on a tie),
     * else the main menu background. Returns {praw bytes, entry name} or null.
     */
    static Object[] pick(Source source) {
        byte[] best = null;
        String name = null;
        for (int i = 0; i < 16; ++i) {
            String entry = "art/splash/splash" + i + ".rix";
            byte[] praw = rixToPraw(source.read(entry));
            if (praw != null && area(praw) > area(best)) { best = praw; name = entry + "@" + source.origin; }
        }
        if (best != null) return new Object[] { best, name };
        byte[] menu = frmToPraw(source.read("art/intrface/mainmenu.frm"), palette(source));
        if (menu != null) return new Object[] { menu, "art/intrface/mainmenu.frm@" + source.origin };
        return null;
    }

    private static byte[] palette(Source source) {
        byte[] pal = source.read("color.pal");
        return pal != null && pal.length >= 768 ? pal : null;
    }

    /** Files that make up the game: its own loose data and dats, then (if the game builds on it) Fallout 2's. */
    static Source sourceFor(File top, String id) {
        Source source = new Source();
        File root = GameRoots.gameRoot(top, id);
        if (root != null) {
            source.looseRoots.add(new File(root, "data"));
            source.looseRoots.add(root);
            File[] dats = root.listFiles((d, n) -> n.toLowerCase(Locale.ROOT).endsWith(".dat"));
            if (dats != null) {
                Arrays.sort(dats, (a, b) -> {
                    boolean am = a.getName().equalsIgnoreCase("master.dat"), bm = b.getName().equalsIgnoreCase("master.dat");
                    return am != bm ? (am ? 1 : -1) : a.getName().compareToIgnoreCase(b.getName()); // patches before master
                });
                source.dats.addAll(Arrays.asList(dats));
            }
        }
        // Fallout 2 based games (Resurrection) fall back to the player's Fallout 2 files.
        if ("resurrection".equals(id)) {
            File base = GameRoots.baseRoot(top, "fallout2");
            File master = base == null ? null : ci(base, "master.dat");
            if (master != null) source.dats.add(master);
        }
        return source;
    }

    static boolean isOwn(File dir) {
        File credit = new File(dir, "credit.txt");
        if (!credit.isFile()) return false;
        try { return new String(Files.readAllBytes(credit.toPath()), StandardCharsets.UTF_8).contains("Image: " + MARKER); }
        catch (IOException e) { return false; }
    }

    /**
     * Writes dir/art.rgba and credit.txt from the game's own files (credit
     * first, then art, atomically). False when the game has no usable picture.
     */
    static boolean generate(File top, String id, File dir, String creator, String projectUrl) throws IOException {
        Object[] picked = pick(sourceFor(top, id));
        if (picked == null) return false;
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("picture directory");
        File art = new File(dir, "art.rgba.part"), credit = new File(dir, "credit.txt.part");
        Files.write(art.toPath(), (byte[]) picked[0]);
        Files.write(credit.toPath(), ("by " + creator + " - " + projectUrl + "\nImage: " + MARKER + picked[1] + "\n")
            .getBytes(StandardCharsets.UTF_8));
        Files.move(credit.toPath(), new File(dir, "credit.txt").toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        Files.move(art.toPath(), new File(dir, "art.rgba").toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        return true;
    }
}
