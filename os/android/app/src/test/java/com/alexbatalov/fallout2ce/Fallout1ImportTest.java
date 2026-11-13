package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

// "Add Fallout 1 files" on synthetic DATs: no real game files are used.
public class Fallout1ImportTest {
    @Rule
    public TemporaryFolder temporary = new TemporaryFolder();

    // Fallout 1 DAT: the directory count (big endian) first, no footer.
    static byte[] dat1(int marker) {
        ByteBuffer buffer = ByteBuffer.allocate(64).order(ByteOrder.BIG_ENDIAN);
        buffer.putInt(3).putInt(0).putInt(0).putInt(marker);
        return buffer.array();
    }

    // Fallout 2 DAT: tree size and file size (little endian) last.
    static byte[] dat2(int marker) {
        ByteBuffer buffer = ByteBuffer.allocate(64).order(ByteOrder.LITTLE_ENDIAN);
        buffer.putInt(0, marker);
        buffer.putInt(56, 20);
        buffer.putInt(60, 64);
        return buffer.array();
    }

    private GameFilesTest.MemorySource source(byte[] master, byte[] critter) {
        GameFilesTest.MemorySource source = new GameFilesTest.MemorySource();
        source.files.put("Fallout/MASTER.DAT", master);
        source.files.put("Fallout/critter.dat", critter);
        source.files.put("Fallout/data/sound/music/01.acm", "MUSIC".getBytes(StandardCharsets.ISO_8859_1));
        source.files.put("Fallout/data/SAVEGAME/SLOT01/SAVE.DAT", "SAVE".getBytes(StandardCharsets.ISO_8859_1));
        source.files.put("Fallout/falloutw.exe", "EXE".getBytes(StandardCharsets.ISO_8859_1));
        source.files.put("Fallout/manual.pdf", "PDF".getBytes(StandardCharsets.ISO_8859_1));
        return source;
    }

    private void install(GameFilesTest.MemorySource source, File top) throws IOException {
        GameImport.Found found = Fallout1Import.find(source);
        assertNotNull(found);
        GameFiles.Plan plan = Fallout1Import.plan(found, top);
        Fallout1Import.install(source, found, plan, top, GameFilesTest.NO_PROGRESS);
    }

    @Test
    public void targetPaths() throws IOException {
        File top = temporary.newFolder("files");
        assertEquals(new File(top, "base/fallout1"), Fallout1Import.baseDir(top));
        assertFalse(Fallout1Import.isInstalled(top));
        assertEquals("master.dat", Fallout1Import.destination("MASTER.DAT"));
        assertEquals("critter.dat", Fallout1Import.destination("Critter.Dat"));
        assertEquals("data/sound/music/01.acm", Fallout1Import.destination("DATA/sound/music/01.acm"));
        assertNull(Fallout1Import.destination("data/SAVEGAME/SLOT01/SAVE.DAT"));
        assertNull(Fallout1Import.destination("falloutw.exe"));
        assertNull(Fallout1Import.destination("patch000.dat"));
    }

    @Test
    public void datKinds() throws IOException {
        File dir = temporary.newFolder("dats");
        File one = new File(dir, "one.dat");
        File two = new File(dir, "two.dat");
        File junk = new File(dir, "junk.dat");
        Files.write(one.toPath(), dat1(7));
        Files.write(two.toPath(), dat2(3));
        Files.write(junk.toPath(), new byte[64]);
        assertEquals(Fallout1Import.DatKind.FALLOUT1, Fallout1Import.datKind(one));
        assertEquals(Fallout1Import.DatKind.FALLOUT2, Fallout1Import.datKind(two));
        assertEquals(Fallout1Import.DatKind.UNKNOWN, Fallout1Import.datKind(junk));
        Files.write(junk.toPath(), new byte[4]);
        assertEquals(Fallout1Import.DatKind.UNKNOWN, Fallout1Import.datKind(junk));
    }

    @Test
    public void findKeepsOnlyABasesFiles() throws IOException {
        GameImport.Found found = Fallout1Import.find(source(dat1(1), dat1(2)));
        assertNotNull(found);
        assertEquals("Fallout/", found.root);
        assertEquals(3, found.files.size());
        assertEquals(0, found.saves);
        assertNull(Fallout1Import.find(new GameFilesTest.MemorySource().put("a/readme.txt", "x")));
    }

    @Test
    public void refusesFallout2ByPatchFile() throws IOException {
        GameFilesTest.MemorySource source = source(dat1(1), dat1(2));
        source.files.put("Fallout/patch000.dat", new byte[8]);
        try {
            Fallout1Import.find(source);
            fail();
        } catch (Fallout1Import.NotFallout1Exception e) {
            assertTrue(e.fallout2);
        }
    }

    @Test
    public void installsToTheBaseOnly() throws IOException {
        File top = temporary.newFolder("files");
        Files.write(new File(top, "master.dat").toPath(), dat2(9));
        Files.write(new File(top, "critter.dat").toPath(), dat2(8));
        install(source(dat1(1), dat1(2)), top);

        File base = new File(top, "base/fallout1");
        assertArrayEquals(dat1(1), Files.readAllBytes(new File(base, "master.dat").toPath()));
        assertArrayEquals(dat1(2), Files.readAllBytes(new File(base, "critter.dat").toPath()));
        assertTrue(new File(base, "data/sound/music/01.acm").isFile());
        assertFalse(new File(base, "data/SAVEGAME").exists());
        assertFalse(new File(base, "falloutw.exe").exists());
        assertFalse(new File(base, GameFiles.STAGING).exists());
        assertTrue(Fallout1Import.isInstalled(top));
        // Fallout 2 at the top is as it was.
        assertArrayEquals(dat2(9), Files.readAllBytes(new File(top, "master.dat").toPath()));
        assertArrayEquals(dat2(8), Files.readAllBytes(new File(top, "critter.dat").toPath()));
        assertEquals(new java.util.TreeSet<>(java.util.Arrays.asList("base", "critter.dat", "master.dat")),
            new java.util.TreeSet<>(java.util.Arrays.asList(top.list())));
    }

    @Test
    public void refusesFallout2DatsAndKeepsTheBase() throws IOException {
        File top = temporary.newFolder("files");
        install(source(dat1(1), dat1(2)), top);
        try {
            install(source(dat2(5), dat1(6)), top);
            fail();
        } catch (Fallout1Import.NotFallout1Exception e) {
            assertTrue(e.fallout2);
        }
        File base = new File(top, "base/fallout1");
        assertArrayEquals(dat1(1), Files.readAllBytes(new File(base, "master.dat").toPath()));
        assertArrayEquals(dat1(2), Files.readAllBytes(new File(base, "critter.dat").toPath()));
        assertFalse(new File(base, GameFiles.STAGING).exists());
        assertTrue(Fallout1Import.isInstalled(top));
    }

    @Test
    public void refusesFallout2OnAFirstImportLeavesNothing() throws IOException {
        File top = temporary.newFolder("files");
        try {
            install(source(dat2(5), dat2(6)), top);
            fail();
        } catch (Fallout1Import.NotFallout1Exception e) {
            assertTrue(e.fallout2);
        }
        assertFalse(Fallout1Import.isInstalled(top));
        assertFalse(new File(top, "base/fallout1/master.dat").exists());
        assertFalse(new File(top, "base/fallout1/" + GameFiles.STAGING).exists());
    }

    @Test
    public void reimportReplacesOnlyAfterSuccess() throws IOException {
        File top = temporary.newFolder("files");
        install(source(dat1(1), dat1(2)), top);
        // A failure half way keeps the first import.
        GameFilesTest.MemorySource broken = source(dat1(3), dat1(4));
        broken.chunk = 16;
        broken.failOn = "Fallout/critter.dat";
        try {
            install(broken, top);
            fail();
        } catch (IOException expected) {
            assertFalse(expected instanceof Fallout1Import.NotFallout1Exception);
        }
        File base = new File(top, "base/fallout1");
        assertArrayEquals(dat1(1), Files.readAllBytes(new File(base, "master.dat").toPath()));
        assertFalse(new File(base, GameFiles.STAGING).exists());
        // A good one replaces it.
        install(source(dat1(3), dat1(4)), top);
        assertArrayEquals(dat1(3), Files.readAllBytes(new File(base, "master.dat").toPath()));
        assertArrayEquals(dat1(4), Files.readAllBytes(new File(base, "critter.dat").toPath()));
    }
}
