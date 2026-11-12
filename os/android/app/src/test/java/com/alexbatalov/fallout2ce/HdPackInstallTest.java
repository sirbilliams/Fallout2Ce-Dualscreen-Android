package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import static org.junit.Assert.*;

public class HdPackInstallTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    private static final HdPackInstall.Progress PROGRESS = new HdPackInstall.Progress() {
        public void update(long done, long total, String path) { }
        public boolean isCancelled() { return false; }
    };

    static String packJson(int version, String tiers) {
        return "{\"version\":" + version + ",\"source\":\"art/critters/x.frm\",\"category\":\"critter\","
            + "\"tiers\":" + tiers + ",\"layout\":[8,8],\"rotations\":[{\"frames\":[[0,0,8,8,123]]}]}";
    }

    static byte[] dds() {
        byte[] bytes = new byte[200];
        bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
        return bytes;
    }

    // A tiny synthetic pack: one FRM at tier 2 plus the marker.
    private GameFilesTest.MemorySource pack(String prefix, int tier) {
        GameFilesTest.MemorySource source = new GameFilesTest.MemorySource();
        source.put(prefix + "pack_" + tier + "x.json", "{}");
        source.put(prefix + "art/critters/x/pack.json", packJson(1, "[2,3,4]"));
        source.files.put(prefix + "art/critters/x/albedo_" + tier + "x.dds", dds());
        return source;
    }

    @Test public void validPackPlansItsTier() throws IOException {
        HdPackInstall.Plan plan = HdPackInstall.validate(pack("hd/", 2));
        assertEquals(2, plan.tier);
        assertEquals(1, plan.packs);
        assertEquals(3, plan.files.size());
        assertTrue(HdPackInstall.looksLikeHdPack(pack("hd/", 3).list()));
        assertTrue(HdPackInstall.looksLikeHdPack(pack("wrapper/hd/", 3).list()));
    }

    @Test public void modZipIsNotAnHdPack() {
        GameFilesTest.MemorySource mod = new GameFilesTest.MemorySource().put("data/proto/x.pro", "x").put("mod.dat", "x");
        assertFalse(HdPackInstall.looksLikeHdPack(mod.list()));
    }

    private void invalid(GameFilesTest.MemorySource source) {
        try {
            HdPackInstall.validate(source);
            fail("accepted");
        } catch (IOException expected) {
        }
    }

    @Test public void missingDdsIsRejected() {
        GameFilesTest.MemorySource source = pack("hd/", 2);
        source.files.remove("hd/art/critters/x/albedo_2x.dds");
        source.put("hd/art/critters/y/albedo_2x.dds", "x");
        invalid(source);
    }

    @Test public void tooSmallOrOrphanDdsIsRejected() {
        GameFilesTest.MemorySource tiny = pack("hd/", 2);
        tiny.put("hd/art/critters/x/albedo_2x.dds", "DDS ");
        invalid(tiny);
        GameFilesTest.MemorySource orphan = pack("hd/", 2);
        orphan.files.put("hd/art/critters/z/albedo_2x.dds", dds());
        invalid(orphan);
    }

    @Test public void badSchemaIsRejected() {
        for (String json : new String[] { packJson(2, "[2]"), packJson(1, "[3,4]"), "{}", "[]", "not json",
            "{\"version\":1,\"tiers\":[2],\"layout\":[0,8],\"rotations\":[]}",
            "{\"version\":1,\"tiers\":[2],\"layout\":[8,8]}", packJson(1, "[2]") + "x" }) {
            GameFilesTest.MemorySource source = pack("hd/", 2);
            source.put("hd/art/critters/x/pack.json", json);
            invalid(source);
        }
    }

    @Test public void mixedOrNoTiersAreRejected() {
        GameFilesTest.MemorySource mixed = pack("hd/", 2);
        mixed.files.put("hd/art/critters/x/albedo_3x.dds", dds());
        invalid(mixed);
        invalid(new GameFilesTest.MemorySource().put("hd/art/critters/x/pack.json", packJson(1, "[2]")));
        GameFilesTest.MemorySource five = pack("hd/", 5);
        invalid(five);
    }

    @Test public void badPathsAreRejected() {
        for (String bad : new String[] { "/hd/art/x/pack.json", "hd/../evil/pack.json", "hd/art\\x/pack.json",
            "hd/c:/pack.json", "hd//x/pack.json", "hd/./x/pack.json" }) {
            GameFilesTest.MemorySource source = pack("hd/", 2);
            source.put(bad, packJson(1, "[2]"));
            invalid(source);
        }
    }

    @Test public void installUnpacksAndReplacesTier() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.Result result = HdPackInstall.install(pack("hd/", 2), "pack-2x.zip", game, PROGRESS);
        assertEquals(2, result.tier);
        File dds = new File(game, "hd/2x/art/critters/x/albedo_2x.dds");
        assertEquals(200, dds.length());
        assertEquals(2, HdPackInstall.installedTier(game));
        assertTrue(HdPackInstall.sizeOf(HdPackInstall.tierDir(game, 2)) >= 200);
        // A second tier replaces the first; an older stale file is gone.
        Files.write(new File(game, "hd/2x/stale.txt").toPath(), "x".getBytes(StandardCharsets.UTF_8));
        HdPackInstall.install(pack("hd/", 3), "pack-3x.zip", game, PROGRESS);
        assertEquals(3, HdPackInstall.installedTier(game));
        assertFalse(new File(game, "hd/2x").exists());
        // Same tier again: replaced as a whole.
        Files.write(new File(game, "hd/3x/stale.txt").toPath(), "x".getBytes(StandardCharsets.UTF_8));
        HdPackInstall.install(pack("hd/", 3), "pack-3x.zip", game, PROGRESS);
        assertFalse(new File(game, "hd/3x/stale.txt").exists());
        String[] left = new File(game, "hd").list();
        assertArrayEquals(new String[] { "3x" }, left);
    }

    @Test public void partZipMergesIntoTier() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(pack("hd/", 2), "a-art-2x-part1.zip", game, PROGRESS);
        GameFilesTest.MemorySource second = new GameFilesTest.MemorySource();
        second.put("hd/pack_2x.json", "{}");
        second.put("hd/art/critters/y/pack.json", packJson(1, "[2]"));
        second.files.put("hd/art/critters/y/albedo_2x.dds", dds());
        HdPackInstall.install(second, "a-art-2x-part2.zip", game, PROGRESS);
        assertTrue(new File(game, "hd/2x/art/critters/x/albedo_2x.dds").isFile());
        assertTrue(new File(game, "hd/2x/art/critters/y/albedo_2x.dds").isFile());
    }

    @Test public void notADdsFailsAndLeavesTierAlone() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(pack("hd/", 2), "p.zip", game, PROGRESS);
        GameFilesTest.MemorySource bad = pack("hd/", 2);
        byte[] junk = new byte[200];
        bad.files.put("hd/art/critters/x/albedo_2x.dds", junk);
        try {
            HdPackInstall.install(bad, "p.zip", game, PROGRESS);
            fail("accepted");
        } catch (IOException expected) {
        }
        assertEquals(200, new File(game, "hd/2x/art/critters/x/albedo_2x.dds").length());
        assertEquals(1, new File(game, "hd").list().length);
    }

    @Test public void cleanupRestoresReplacedTierAndDropsTemp() throws IOException {
        File game = temporary.newFolder("game");
        File old = new File(game, "hd/.old-2x");
        assertTrue(old.mkdirs());
        assertTrue(new File(game, "hd/.tmp-3x").mkdirs());
        HdPackInstall.cleanup(game);
        assertTrue(new File(game, "hd/2x").isDirectory());
        assertFalse(new File(game, "hd/.tmp-3x").exists());
    }

    @Test public void removeTakesOnlyTheTier() throws IOException {
        File game = temporary.newFolder("game");
        HdPackInstall.install(pack("hd/", 2), "p.zip", game, PROGRESS);
        File other = new File(game, "hd/keep.txt");
        Files.write(other.toPath(), "x".getBytes(StandardCharsets.UTF_8));
        assertTrue(HdPackInstall.remove(game, 2));
        assertEquals(0, HdPackInstall.installedTier(game));
        assertTrue(other.isFile());
        assertFalse(HdPackInstall.remove(game, 2));
    }

    @Test public void settingsWriteHdKeys() throws IOException {
        File game = temporary.newFolder("game");
        Files.write(new File(game, "fallout2.cfg").toPath(), "[system]\nlanguage=english\n".getBytes(StandardCharsets.ISO_8859_1));
        assertFalse(HdSettings.isEnabled(game));
        assertEquals("high", HdSettings.quality(game));
        HdPackInstall.install(pack("hd/", 2), "p.zip", game, PROGRESS);
        HdSettings.setEnabled(game, true);
        HdSettings.setQuality(game, "medium");
        assertTrue(HdSettings.isEnabled(game));
        assertEquals("medium", HdSettings.quality(game));
        HdSettings.setQuality(game, "off");
        assertEquals("off", HdSettings.quality(game));
        HdSettings.setEnabled(game, false);
        String cfg = new String(Files.readAllBytes(new File(game, "fallout2.cfg").toPath()), StandardCharsets.ISO_8859_1);
        assertTrue(cfg, cfg.contains("[hd]\nenabled=0\nhd_art_dir=hd/2x"));
        assertTrue(cfg.contains("language=english"));
        try {
            HdSettings.setQuality(game, "ultra");
            fail("accepted");
        } catch (IOException expected) {
        }
    }
}
