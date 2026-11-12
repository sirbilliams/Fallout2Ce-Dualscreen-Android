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

    // --- fonts-only last part (release.py appends hd/fonts after the art packs) ---

    static final String PF = "Dustlight HD-193-1-0-art-2x-part4.zip";

    static GameFilesTest.MemorySource fonts(int tier, boolean index, String... atlases) {
        GameFilesTest.MemorySource zip = new GameFilesTest.MemorySource();
        if (index) zip.put("hd/fonts/fonts.json", "{\"fonts\":[]}");
        for (String atlas : atlases) zip.put("hd/fonts/" + atlas + "-" + tier + "x.png", "\u0089PNG\r\n\u001a\n0000");
        return zip;
    }

    @Test public void fontsOnlyPartMergesIntoItsTierAndIsCounted() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(p1(), P1, game, PROGRESS, false);
        HdPackInstall.install(p2(), P2, game, PROGRESS, false);
        HdPackInstall.install(p3(), P3, game, PROGRESS, false);
        assertTrue(DustlightHd.status(game).complete());
        assertEquals(0, DustlightHd.status(game).fontFiles);

        HdPackInstall.Result r = HdPackInstall.install(fonts(2, true, "font0-model", "font1-model"), PF, game, PROGRESS, false);
        assertEquals(2, r.tier);
        assertEquals(0, r.packs);
        assertTrue(new File(game, "hd/2x/fonts/font0-model-2x.png").isFile());
        assertTrue(new File(game, "hd/2x/fonts/fonts.json").isFile());
        DustlightHd.Status s = DustlightHd.status(game);
        assertEquals(5, s.packs);                  // fonts add no packs
        assertEquals(3, s.parts.size());           // art parts only
        assertEquals(1, s.fontParts.size());
        assertEquals(PF, s.fontParts.get(0));
        assertEquals(2, s.fontFiles);
        assertTrue(s.complete());
        assertTrue(new File(game, "hd/2x/art/critters/e/albedo_2x.dds").isFile());
        assertFalse(new File(game, "hd/2x/pack.json").exists());
    }

    @Test public void fontsPartMayComeFirstAndKeepsTheOtherPartsOut() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(fonts(2, false, "font0-model"), PF, game, PROGRESS, false);
        assertEquals(2, DustlightHd.status(game).tier);
        assertEquals(0, DustlightHd.status(game).packs);
        HdPackInstall.install(p1(), P1, game, PROGRESS, false);
        assertEquals(1, DustlightHd.status(game).fontFiles);
        assertEquals(2, DustlightHd.status(game).packs);
        assertEquals(1, DustlightHd.status(game).fontParts.size());
    }

    @Test public void fontsOfAnotherTierAreBlockedLikeArtAndNothingIsWritten() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(p1(), P1, game, PROGRESS, false);
        try {
            HdPackInstall.install(fonts(3, false, "font0-model"), "Dustlight HD-193-1-0-art-3x-part4.zip", game, PROGRESS, false);
            fail("expected a tier mismatch");
        } catch (HdPackInstall.TierMismatch m) {
            assertEquals(3, m.incoming);
        }
        assertFalse(new File(game, "hd/3x").exists());
        assertEquals(0, DustlightHd.status(game).fontFiles);
    }

    @Test public void fontsNamedForAnotherTierThanTheyHoldAreInvalid() throws IOException {
        try {
            HdPackInstall.validate(fonts(3, false, "font0-model"), "Dustlight HD-193-1-0-art-2x-part4.zip");
            fail("expected Invalid");
        } catch (HdPackInstall.Invalid expected) { }
    }

    @Test public void fontsOfTwoTiersInOneZipAreInvalid() throws IOException {
        GameFilesTest.MemorySource zip = fonts(2, false, "font0-model");
        zip.put("hd/fonts/font0-model-3x.png", "\u0089PNG0000");
        try {
            HdPackInstall.validate(zip, PF);
            fail("expected Invalid");
        } catch (HdPackInstall.Invalid expected) { }
    }

    @Test public void artPartWithFontsOfItsOwnTierInstallsThemAndOtherTierFontsAreInvalid() throws IOException {
        File game = temporary.newFolder("game");
        GameFilesTest.MemorySource mixed = part(2, 5, "a");
        mixed.put("hd/fonts/fonts.json", "{}").put("hd/fonts/font0-model-2x.png", "\u0089PNG0000");
        HdPackInstall.install(mixed, P1, game, PROGRESS, false);
        assertEquals(1, DustlightHd.status(game).fontFiles);
        GameFilesTest.MemorySource wrong = part(2, 5, "b");
        wrong.put("hd/fonts/font0-model-3x.png", "\u0089PNG0000");
        try {
            HdPackInstall.validate(wrong, P2);
            fail("expected Invalid");
        } catch (HdPackInstall.Invalid expected) { }
    }

    @Test public void zipWithNeitherArtNorFontsIsRejected() throws IOException {
        File game = temporary.newFolder("game");
        for (GameFilesTest.MemorySource zip : new GameFilesTest.MemorySource[] {
            new GameFilesTest.MemorySource().put("hd/readme.txt", "x"),
            new GameFilesTest.MemorySource().put("hd/fonts/fonts.json", "{}"),               // index alone is not a part
            new GameFilesTest.MemorySource().put("hd/fonts/notes.txt", "x"),
            fonts(2, false, "font0-model").put("hd/other/stuff.bin", "x") }) {
            try {
                HdPackInstall.install(zip, PF, game, PROGRESS, false);
                fail("expected Invalid");
            } catch (HdPackInstall.Invalid expected) { }
        }
        assertEquals(0, HdPackInstall.installedTier(game));
    }

    @Test public void fontAtlasMustBeAPng() throws IOException {
        File game = temporary.newFolder("game");
        GameFilesTest.MemorySource zip = new GameFilesTest.MemorySource().put("hd/fonts/font0-model-2x.png", "not a png at all");
        try {
            HdPackInstall.install(zip, PF, game, PROGRESS, false);
            fail("expected Invalid");
        } catch (HdPackInstall.Invalid expected) { }
        assertEquals(0, HdPackInstall.installedTier(game));
    }

    @Test public void fontsPartIsRecognisedByContentToo() {
        assertTrue(DustlightHd.isHdContent(fonts(2, true, "font0-model").list()));
        assertFalse(DustlightHd.isHdContent(new GameFilesTest.MemorySource().put("hd/fonts/fonts.json", "{}").list()));
    }
}
