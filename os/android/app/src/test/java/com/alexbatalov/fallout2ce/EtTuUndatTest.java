package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class EtTuUndatTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    private File top;

    private static class Fake implements EtTuUndat.Extractor {
        int calls;
        boolean dat1 = true;
        boolean fail;
        int written = 3;
        List<String> lastEntries;
        File lastOut;
        boolean lastLower;

        @Override public boolean isDat1(File dat) { return dat1; }

        @Override public Dat1Extractor.Result extract(File dat, List<String> entries, File outDir, boolean lowerCase) throws IOException {
            calls++;
            lastEntries = entries;
            lastOut = outDir;
            lastLower = lowerCase;
            if (fail) throw new IOException("bad archive");
            return new Dat1Extractor.Result(written, new ArrayList<>());
        }
    }

    private void write(File f, String text) throws IOException {
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    @Before public void setUp() throws IOException {
        top = tmp.newFolder("top");
        write(new File(top, "games/ettu/undat_files.txt"), "ART\\SCENERY\\DOOR04.FRM\n\n  ART\\X.FRM \n");
        write(new File(top, "base/fallout1/master.dat"), "master-one");
    }

    @Test public void notApplicableWithoutInputs() throws Exception {
        Fake fake = new Fake();
        assertEquals(EtTuUndat.Outcome.NOT_APPLICABLE, EtTuUndat.run(tmp.newFolder("empty"), fake));
        new File(top, "base/fallout1/master.dat").delete();
        assertFalse(EtTuUndat.needed(top));
        assertEquals(EtTuUndat.Outcome.NOT_APPLICABLE, EtTuUndat.run(top, fake));
        assertEquals(0, fake.calls);
    }

    @Test public void runsWhenNeededThenSkipsWhileMarkerFresh() throws Exception {
        Fake fake = new Fake();
        assertTrue(EtTuUndat.needed(top));
        assertEquals(EtTuUndat.Outcome.UNPACKED, EtTuUndat.run(top, fake));
        assertEquals(1, fake.calls);
        assertEquals(2, fake.lastEntries.size());
        assertEquals("ART\\X.FRM", fake.lastEntries.get(1));
        assertEquals(new File(top, "games/ettu/data"), fake.lastOut);
        assertTrue(fake.lastLower);
        assertTrue(new File(top, "games/ettu/.undat-done").isFile());
        assertFalse(EtTuUndat.needed(top));
        assertEquals(EtTuUndat.Outcome.UP_TO_DATE, EtTuUndat.run(top, fake));
        assertEquals(1, fake.calls);
    }

    @Test public void rerunsWhenMasterDatChanges() throws Exception {
        Fake fake = new Fake();
        EtTuUndat.run(top, fake);
        write(new File(top, "base/fallout1/master.dat"), "master-two-longer");
        assertTrue(EtTuUndat.needed(top));
        assertEquals(EtTuUndat.Outcome.UNPACKED, EtTuUndat.run(top, fake));
        assertEquals(2, fake.calls);
        // Same size, newer time.
        File master = new File(top, "base/fallout1/master.dat");
        assertTrue(master.setLastModified(master.lastModified() + 5000));
        assertTrue(EtTuUndat.needed(top));
    }

    @Test public void rerunsWhenListChanges() throws Exception {
        Fake fake = new Fake();
        EtTuUndat.run(top, fake);
        write(new File(top, "games/ettu/undat_files.txt"), "ART\\ONLY.FRM\n");
        assertTrue(EtTuUndat.needed(top));
    }

    @Test public void noMarkerOnFailure() throws Exception {
        Fake fake = new Fake();
        fake.fail = true;
        try {
            EtTuUndat.run(top, fake);
            fail();
        } catch (IOException expected) {
            assertEquals("bad archive", expected.getMessage());
        }
        assertFalse(new File(top, "games/ettu/.undat-done").exists());
        assertTrue(EtTuUndat.needed(top));
        fake.fail = false;
        assertEquals(EtTuUndat.Outcome.UNPACKED, EtTuUndat.run(top, fake));
    }

    @Test public void noMarkerWhenNotDat1OrNothingWritten() throws Exception {
        Fake fake = new Fake();
        fake.dat1 = false;
        try {
            EtTuUndat.run(top, fake);
            fail();
        } catch (IOException expected) {
        }
        assertEquals(0, fake.calls);
        fake.dat1 = true;
        fake.written = 0;
        try {
            EtTuUndat.run(top, fake);
            fail();
        } catch (IOException expected) {
        }
        assertFalse(new File(top, "games/ettu/.undat-done").exists());
    }
}
