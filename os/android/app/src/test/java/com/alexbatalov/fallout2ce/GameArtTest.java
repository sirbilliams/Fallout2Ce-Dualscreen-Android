package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.zip.Deflater;
import static org.junit.Assert.*;

public class GameArtTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    static byte[] rix(int w, int h, int pixelValue) {
        byte[] b = new byte[10 + 768 + w * h];
        b[0] = 'R'; b[1] = 'I'; b[2] = 'X'; b[3] = '3';
        b[4] = (byte) w; b[5] = (byte) (w >> 8); b[6] = (byte) h; b[7] = (byte) (h >> 8);
        b[10 + 3 * 1] = 63; // palette[1] = red (6-bit)
        b[10 + 3 * 2 + 1] = 63; // palette[2] = green
        for (int i = 0; i < w * h; ++i) b[10 + 768 + i] = (byte) pixelValue;
        return b;
    }

    static void dat2(File file, Map<String, byte[]> entries, boolean compress) throws IOException {
        ByteArrayOutputStream data = new ByteArrayOutputStream();
        ByteArrayOutputStream tree = new ByteArrayOutputStream();
        ByteBuffer count = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(entries.size());
        tree.write(count.array());
        for (Map.Entry<String, byte[]> e : entries.entrySet()) {
            byte[] raw = e.getValue(), stored = raw;
            if (compress) {
                Deflater d = new Deflater();
                d.setInput(raw);
                d.finish();
                byte[] buf = new byte[raw.length + 64];
                int n = d.deflate(buf);
                d.end();
                stored = Arrays.copyOf(buf, n);
            }
            byte[] name = e.getKey().replace('/', '\\').getBytes(StandardCharsets.ISO_8859_1);
            ByteBuffer t = ByteBuffer.allocate(4 + name.length + 13).order(ByteOrder.LITTLE_ENDIAN);
            t.putInt(name.length).put(name).put((byte) (compress ? 1 : 0)).putInt(raw.length).putInt(stored.length).putInt(data.size());
            tree.write(t.array());
            data.write(stored);
        }
        ByteBuffer tail = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN);
        long total = data.size() + tree.size() + 8;
        tail.putInt(tree.size()).putInt((int) total);
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(data.toByteArray());
            out.write(tree.toByteArray());
            out.write(tail.array());
        }
    }

    @Test public void rixDecodesToPraw() {
        byte[] praw = GameArt.rixToPraw(rix(4, 2, 1));
        ByteBuffer b = ByteBuffer.wrap(praw);
        assertEquals(0x50524157, b.getInt());
        assertEquals(4, b.getInt());
        assertEquals(2, b.getInt());
        assertEquals(12 + 4 * 2 * 4, praw.length);
        assertEquals((byte) 255, praw[12]);     // R of palette[1]
        assertEquals((byte) 0, praw[13]);
        assertEquals((byte) 0, praw[14]);
        assertEquals((byte) 255, praw[15]);     // opaque
        assertEquals(255 & 0xFF, praw[12 + 7 * 4 + 3] & 0xFF);
        byte[] green = GameArt.rixToPraw(rix(2, 2, 2));
        assertEquals((byte) 255, green[13]);
        byte[] black = GameArt.rixToPraw(rix(2, 2, 255)); // engine draws index 255 black
        assertEquals(0, black[12] | black[13] | black[14]);
        assertNull(GameArt.rixToPraw(new byte[20]));
        byte[] truncated = Arrays.copyOf(rix(4, 4, 1), 790);
        assertNull(GameArt.rixToPraw(truncated));
    }

    @Test public void datReaderFindsCompressedAndStoredEntriesCaseInsensitively() throws Exception {
        File dat = temp.newFile("master.dat");
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("ART/SPLASH/splash0.rix", rix(4, 2, 1));
        entries.put("other.txt", "hello".getBytes(StandardCharsets.UTF_8));
        dat2(dat, entries, true);
        assertArrayEquals(rix(4, 2, 1), GameArt.readDat2(dat, "art/splash/splash0.rix"));
        assertNull(GameArt.readDat2(dat, "art/splash/splash9.rix"));
        File stored = temp.newFile("plain.dat");
        dat2(stored, entries, false);
        assertArrayEquals("hello".getBytes(StandardCharsets.UTF_8), GameArt.readDat2(stored, "other.txt"));
        File junk = temp.newFile("junk.dat");
        Files.write(junk.toPath(), new byte[100]);
        assertNull(GameArt.readDat2(junk, "other.txt"));
    }

    @Test public void generatesArtAndCreditFromTheGamesOwnDatPickingTheRichestSplash() throws Exception {
        File top = temp.newFolder("top");
        File root = new File(new File(top, "games"), "sonora");
        assertTrue(root.mkdirs());
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("art/splash/splash0.rix", rix(4, 2, 3)); // gray: loses on richness
        entries.put("art/splash/splash1.rix", rix(8, 6, 2));
        dat2(new File(root, "master.dat"), entries, true);
        File dir = new File(root, "picker");
        assertTrue(GameArt.generate(top, "sonora", dir, "Someone", "https://example.org/x"));
        byte[] art = Files.readAllBytes(new File(dir, "art.rgba").toPath());
        ByteBuffer b = ByteBuffer.wrap(art);
        assertEquals(0x50524157, b.getInt());
        assertEquals(8, b.getInt());
        assertEquals(6, b.getInt());
        String credit = new String(Files.readAllBytes(new File(dir, "credit.txt").toPath()), StandardCharsets.UTF_8);
        assertTrue(credit.contains("by Someone - https://example.org/x"));
        assertTrue(credit.contains("Image: game-file:art/splash/splash1.rix@master.dat"));
        assertTrue(GameArt.isOwn(dir));
        assertFalse(PickerPictures.purgeSocialCard(dir)); // not mistaken for a GitHub social card
        assertTrue(new File(dir, "art.rgba").isFile());
    }

    @Test public void flatSplashesAreRejectedAndOtherCopiesAreTried() throws Exception {
        File top = temp.newFolder("flat");
        File root = new File(new File(top, "games"), "nevada");
        File splash = new File(root, "data/art/splash");
        assertTrue(splash.mkdirs());
        Files.write(new File(splash, "splash0.rix").toPath(), rix(8, 6, 3)); // flat black
        Files.write(new File(splash, "splash5.rix").toPath(), rix(8, 6, 2)); // green
        Object[] pick = GameArt.pick(GameArt.sourceFor(top, "nevada"));
        assertNotNull(pick);
        assertTrue(pick[1].toString().startsWith("art/splash/splash5.rix"));
        File only = temp.newFolder("flatonly");
        File root2 = new File(new File(only, "games"), "nevada");
        File splash2 = new File(root2, "data/art/splash");
        assertTrue(splash2.mkdirs());
        Files.write(new File(splash2, "splash0.rix").toPath(), rix(8, 6, 3));
        assertNull(GameArt.pick(GameArt.sourceFor(only, "nevada")));
    }

    @Test public void looseSplashAndMainMenuFallback() throws Exception {
        File top = temp.newFolder("top2");
        File root = new File(new File(top, "games"), "sonora");
        File splash = new File(root, "data/Art/Splash");
        assertTrue(splash.mkdirs());
        Files.write(new File(splash, "SPLASH3.RIX").toPath(), rix(4, 4, 1));
        assertEquals(16, GameArt.area(GameArt.rixToPraw(rix(4, 4, 1))) );
        assertTrue(GameArt.generate(top, "sonora", new File(root, "picker"), "c", "u"));
        // No splash: main menu FRM with color.pal.
        File top3 = temp.newFolder("top3");
        File root3 = new File(new File(top3, "games"), "sonora");
        byte[] frm = new byte[62 + 12 + 6];
        ByteBuffer f = ByteBuffer.wrap(frm).order(ByteOrder.BIG_ENDIAN);
        f.putShort(62, (short) 3).putShort(64, (short) 2);
        for (int i = 0; i < 6; ++i) frm[74 + i] = 1;
        byte[] pal = new byte[768];
        pal[3] = 63;
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("art/intrface/mainmenu.frm", frm);
        entries.put("color.pal", pal);
        assertTrue(root3.mkdirs());
        dat2(new File(root3, "master.dat"), entries, true);
        File dir3 = new File(root3, "picker");
        assertTrue(GameArt.generate(top3, "sonora", dir3, "c", "u"));
        byte[] art = Files.readAllBytes(new File(dir3, "art.rgba").toPath());
        assertEquals(12 + 6 * 4, art.length);
        assertEquals((byte) 255, art[12]);
        // Nothing usable: no files written.
        File top4 = temp.newFolder("top4");
        assertTrue(new File(top4, "games/sonora").mkdirs());
        assertFalse(GameArt.generate(top4, "sonora", new File(top4, "games/sonora/picker"), "c", "u"));
        assertFalse(new File(top4, "games/sonora/picker/art.rgba").exists());
    }

    @Test public void gameUniqueColorfulSplashBeatsStockGraySplash() throws Exception {
        File top = temp.newFolder("top5");
        byte[] gray = rix(8, 6, 3);
        Map<String, byte[]> base = new LinkedHashMap<>();
        base.put("art/splash/splash0.rix", gray);
        dat2(new File(top, "master.dat"), base, true);
        File root = new File(new File(top, "games"), "sonora");
        assertTrue(root.mkdirs());
        byte[] colourful = rix(4, 4, 1);
        for (int i = 0; i < 16; ++i) colourful[10 + 768 + i] = (byte) (i % 2 == 0 ? 1 : 2);
        Map<String, byte[]> own = new LinkedHashMap<>();
        own.put("art/splash/splash0.rix", gray);
        own.put("art/splash/splash3.rix", colourful);
        dat2(new File(root, "master.dat"), own, true);
        assertTrue(GameArt.richness(GameArt.rixToPraw(colourful)) > GameArt.richness(GameArt.rixToPraw(gray)));
        File dir = new File(root, "picker");
        assertTrue(GameArt.generate(top, "sonora", dir, "c", "u"));
        String credit = new String(Files.readAllBytes(new File(dir, "credit.txt").toPath()), StandardCharsets.UTF_8);
        assertTrue(credit, credit.contains("splash3.rix"));
        // Only stock images exist: they are still used.
        File top2 = temp.newFolder("top6");
        Map<String, byte[]> stock = new LinkedHashMap<>();
        stock.put("art/splash/splash0.rix", colourful);
        dat2(new File(top2, "master.dat"), stock, true);
        File root2 = new File(new File(top2, "games"), "sonora");
        assertTrue(root2.mkdirs());
        dat2(new File(root2, "master.dat"), stock, true);
        assertTrue(GameArt.generate(top2, "sonora", new File(root2, "picker"), "c", "u"));
    }
}
