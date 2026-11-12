package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import static org.junit.Assert.*;

public class DustlightHdTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    private static final HdPackInstall.Progress PROGRESS = new HdPackInstall.Progress() {
        public void update(long done, long total, String path) { }
        public boolean isCancelled() { return false; }
    };

    // --- fixture generator: release-like zips (as in-memory sources) ---

    // One tier part: [names] pack folders, each with pack.json + albedo_<tier>x.dds;
    // the marker hd/pack_<tier>x.json (with the whole tier's pack count) only
    // when [total] >= 0, as in the real release (first part only).
    static GameFilesTest.MemorySource part(int tier, int total, String... names) {
        GameFilesTest.MemorySource zip = new GameFilesTest.MemorySource();
        if (total >= 0) {
            zip.put("hd/pack_" + tier + "x.json", "{\"name\":\"Dustlight\",\"version\":\"1.0\",\"tier\":" + tier
                + ",\"model\":\"x\",\"packs\":" + total + ",\"bytes\":12345}");
        }
        for (String name : names) {
            zip.put("hd/art/critters/" + name + "/pack.json", HdPackInstallTest.packJson(1, "[2,3,4]"));
            zip.files.put("hd/art/critters/" + name + "/albedo_" + tier + "x.dds", HdPackInstallTest.dds());
        }
        return zip;
    }

    static GameFilesTest.MemorySource engine() {
        return new GameFilesTest.MemorySource().put("dustlight.exe", "MZ").put("README.txt", "windows only");
    }

    // 2x tier: 3 parts, 5 packs in total.
    static final String P1 = "Dustlight HD-193-1-0-art-2x-part1.zip";
    static final String P2 = "Dustlight HD-193-1-0-art-2x-part2.zip";
    static final String P3 = "Dustlight HD-193-1-0-art-2x-part3.zip";
    static GameFilesTest.MemorySource p1() { return part(2, 5, "a", "b"); }
    static GameFilesTest.MemorySource p2() { return part(2, -1, "c", "d"); }
    static GameFilesTest.MemorySource p3() { return part(2, -1, "e"); }

    // --- routing ---

    @Test public void routesByName() {
        assertEquals(DustlightHd.Kind.ART, DustlightHd.classify("dustlight-1.0-art-2x-part1.zip").kind);
        assertEquals(2, DustlightHd.classify("dustlight-1.0-art-2x-part1.zip").tier);
        assertEquals(1, DustlightHd.classify("dustlight-1.0-art-2x-part1.zip").part);
        assertEquals(12, DustlightHd.classify("Dustlight-1.0-ART-4x-PART12.ZIP").part);
        assertEquals(4, DustlightHd.classify("Dustlight-1.0-ART-4x-PART12.ZIP").tier);
        assertEquals(0, DustlightHd.classify("dustlight-1.0-art-3x.zip").part);
        // Nexus may append mod id / version / time.
        assertEquals(DustlightHd.Kind.ART, DustlightHd.classify("dustlight-1.0-art-2x-part3-193-1-0-1700000000.zip").kind);
        assertEquals(3, DustlightHd.classify("dustlight-1.0-art-2x-part3-193-1-0-1700000000.zip").part);
        assertEquals(DustlightHd.Kind.ENGINE, DustlightHd.classify("dustlight-1.0-engine.zip").kind);
        assertEquals(DustlightHd.Kind.OTHER, DustlightHd.classify("some-mod.zip").kind);
        assertEquals(DustlightHd.Kind.OTHER, DustlightHd.classify("dustlight-1.0-art-9x.zip").kind);
        assertEquals(DustlightHd.Kind.OTHER, DustlightHd.classify("dustlight-1.0-art-2x-part1.7z").kind);
        assertEquals(DustlightHd.Kind.OTHER, DustlightHd.classify("my-start-art-2xyz.zip").kind);
    }

    @Test public void routesByContent() {
        assertTrue(DustlightHd.isHdContent(p1().list()));
        assertTrue(DustlightHd.isHdContent(p2().list()));
        // a part that only carries the marker
        assertTrue(DustlightHd.isHdContent(new GameFilesTest.MemorySource().put("hd/pack_2x.json", "{}").list()));
        assertFalse(DustlightHd.isHdContent(engine().list()));
        assertFalse(DustlightHd.isHdContent(new GameFilesTest.MemorySource().put("data/proto/x.pro", "x").list()));
    }

    @Test public void engineZipIsNotAnHdPackAndIsRejectedByInstaller() throws IOException {
        File game = temporary.newFolder("game");
        assertFalse(HdPackInstall.looksLikeHdPack(engine().list()));
        try {
            HdPackInstall.install(engine(), "dustlight-1.0-engine.zip", game, PROGRESS, false);
            fail("accepted");
        } catch (HdPackInstall.Invalid expected) {
        }
        assertEquals(0, HdPackInstall.installedTier(game));
    }

    // --- install order, completeness ---

    @Test public void orderThreeOneTwoEndsCompleteAndEnabled() throws IOException {
        File game = temporary.newFolder("game");
        Files.write(new File(game, "fallout2.cfg").toPath(), "[system]\nlanguage=english\n".getBytes(StandardCharsets.ISO_8859_1));

        DustlightHd.Status s = DustlightHd.status(game);
        assertEquals(0, s.tier);
        assertEquals(-1, s.moreParts());

        // part 3 first: no marker yet, total unknown
        DustlightHd.Status before = DustlightHd.status(game);
        HdPackInstall.install(p3(), P3, game, PROGRESS, false);
        assertFalse(DustlightHd.enableIfNewlyComplete(game, before));
        s = DustlightHd.status(game);
        assertEquals(2, s.tier);
        assertEquals(1, s.packs);
        assertEquals(-1, s.totalPacks);
        assertFalse(s.complete());
        assertEquals(-1, s.moreParts());

        // part 1 (marker): total known, 3 of 5 packs
        before = DustlightHd.status(game);
        HdPackInstall.install(p1(), P1, game, PROGRESS, false);
        assertFalse(DustlightHd.enableIfNewlyComplete(game, before));
        s = DustlightHd.status(game);
        assertEquals(5, s.totalPacks);
        assertEquals(3, s.packs);
        assertEquals(2, s.parts.size());
        assertFalse(s.complete());
        assertEquals(1, s.moreParts());
        assertFalse(HdSettings.isEnabled(game));

        // part 2: complete, HD switched on with the tier folder
        before = DustlightHd.status(game);
        HdPackInstall.install(p2(), P2, game, PROGRESS, false);
        assertTrue(DustlightHd.enableIfNewlyComplete(game, before));
        s = DustlightHd.status(game);
        assertTrue(s.complete());
        assertEquals(5, s.packs);
        assertEquals(0, s.moreParts());
        assertTrue(HdSettings.isEnabled(game));
        String cfg = new String(Files.readAllBytes(new File(game, "fallout2.cfg").toPath()), StandardCharsets.ISO_8859_1);
        assertTrue(cfg, cfg.contains("hd_art_dir=hd/2x"));

        // engine layout: hd/2x/art/... and hd/2x/pack_2x.json
        assertTrue(new File(game, "hd/2x/pack_2x.json").isFile());
        assertTrue(new File(game, "hd/2x/art/critters/e/albedo_2x.dds").isFile());
        assertTrue(new File(game, "hd/2x/art/critters/a/pack.json").isFile());
        assertEquals(1, new File(game, "hd").list().length);

        // a later re-install of a part does not flip anything again
        before = DustlightHd.status(game);
        HdPackInstall.install(p2(), P2, game, PROGRESS, false);
        assertFalse(DustlightHd.enableIfNewlyComplete(game, before));
        assertEquals(5, DustlightHd.status(game).packs);
        assertEquals(3, DustlightHd.status(game).parts.size());
    }

    @Test public void orderOneTwoThreeCompletesOnTheLastPart() throws IOException {
        File game = temporary.newFolder("game");
        DustlightHd.Status before = DustlightHd.status(game);
        HdPackInstall.install(p1(), P1, game, PROGRESS, false);
        assertFalse(DustlightHd.enableIfNewlyComplete(game, before));
        assertEquals(2, DustlightHd.status(game).moreParts());  // 3 missing, 2 per part
        before = DustlightHd.status(game);
        HdPackInstall.install(p2(), P2, game, PROGRESS, false);
        assertFalse(DustlightHd.enableIfNewlyComplete(game, before));
        before = DustlightHd.status(game);
        HdPackInstall.install(p3(), P3, game, PROGRESS, false);
        assertTrue(DustlightHd.enableIfNewlyComplete(game, before));
    }

    @Test public void completenessCountsFromDiskWithoutTheRecord() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(p1(), P1, game, PROGRESS, false);
        assertTrue(new File(game, "hd/2x/" + DustlightHd.PARTS_FILE).delete());
        assertEquals(2, DustlightHd.status(game).packs);
        assertEquals(2, DustlightHd.countPacks(new File(game, "hd/2x")));
    }

    // --- tiers ---

    @Test public void otherTierIsDetectedAndNothingIsWritten() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(p1(), P1, game, PROGRESS, false);
        try {
            HdPackInstall.install(part(4, 5, "z"), "Dustlight HD-193-1-0-art-4x-part1.zip", game, PROGRESS, false);
            fail("mixed tiers");
        } catch (HdPackInstall.TierMismatch m) {
            assertEquals(2, m.installed);
            assertEquals(4, m.incoming);
        }
        assertEquals(2, HdPackInstall.installedTier(game));
        assertFalse(new File(game, "hd/4x").exists());
        assertEquals(2, DustlightHd.status(game).packs);
        assertEquals(1, new File(game, "hd").list().length);
    }

    @Test public void agreedSwitchReplacesTheTier() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(p1(), P1, game, PROGRESS, false);
        HdPackInstall.install(part(4, 5, "z"), "Dustlight HD-193-1-0-art-4x-part1.zip", game, PROGRESS, true);
        assertEquals(4, HdPackInstall.installedTier(game));
        assertFalse(new File(game, "hd/2x").exists());
        assertEquals(1, DustlightHd.status(game).packs);
        assertEquals(5, DustlightHd.status(game).totalPacks);
    }

    @Test public void partOfSameTierMergesEvenWithNexusSuffix() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(p1(), "Dustlight HD-193-1-0-art-2x-part1-193-1-0-17000.zip", game, PROGRESS, false);
        HdPackInstall.install(p2(), "Dustlight HD-193-1-0-art-2x-part2-193-1-0-17001.zip", game, PROGRESS, false);
        assertEquals(4, DustlightHd.status(game).packs);
        assertTrue(new File(game, "hd/2x/art/critters/a/albedo_2x.dds").isFile());
    }

    @Test public void planSizeIsTheSumOfItsFiles() throws IOException {
        // Free space is checked per part by install() against plan.bytes + margin.
        HdPackInstall.Plan plan = HdPackInstall.validate(p1());
        assertEquals(2, plan.packs);
        assertTrue(plan.bytes >= 400);
    }
}
