package com.alexbatalov.fallout2ce;

import org.junit.Test;
import org.junit.Rule;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.*;
import static org.junit.Assert.*;

public class GameInstallTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    @Test public void recordedSonoraReleaseSelectsFullGame() throws Exception {
        String json;
        try (InputStream in = getClass().getResourceAsStream("/game-releases/sonora.json")) {
            assertNotNull(in);
            json = new String(in.readAllBytes(), StandardCharsets.UTF_8);
        }
        RpuRelease.Asset a = GameInstall.asset("sonora", json);
        assertEquals("Fallout_Sonora_1.16.4E_sfall.zip", a.name);
        assertEquals(1450087262L, a.size);
        assertTrue(a.url.startsWith("https://github.com/NovaRain/Fallout-Sonora-English/releases/download/"));
    }

    @Test public void ettuSelectionAndOfficialHostOnly() throws Exception {
        String prefix = "https://github.com/rotators/Fo1in2/releases/download/v2/";
        String json = "{\"assets\":[{\"name\":\"source.zip\",\"size\":999,\"browser_download_url\":\"" + prefix + "source.zip\"},"
            + "{\"name\":\"Fallout1in2.zip\",\"size\":123,\"browser_download_url\":\"" + prefix + "Fallout1in2.zip\"}]}";
        assertEquals(123, GameInstall.asset("ettu", json).size);
        fails(() -> GameInstall.asset("ettu", json.replace(prefix, "https://evil.example/")));
        fails(() -> GameInstall.asset("ettu", "{\"draft\":true,\"assets\":[]}"));
    }

    @Test public void spaceIncludesArchiveExtractedAndMarginWithoutOverflow() throws Exception {
        assertEquals(67108864L + 300, GameInstall.needed(100, 200));
        assertEquals(67108864L + 500, GameInstall.preflight(100));
        assertTrue(GameInstall.enoughSpace(67108864L + 300, 100, 200));
        assertFalse(GameInstall.enoughSpace(67108864L + 299, 100, 200));
        fails(() -> GameInstall.preflight(Long.MAX_VALUE));
        fails(() -> GameInstall.needed(0, 1));
        fails(() -> GameInstall.needed(Long.MAX_VALUE, 2));
    }

    private File zip(String... paths) throws Exception {
        File zip = temp.newFile();
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(zip))) {
            for (String path : paths) {
                out.putNextEntry(new ZipEntry(path));
                out.write((path.toLowerCase().endsWith("cfg") ? "[system]\nmaster_dat=C:\\Fallout2\\master.dat\n[other]\nvalue=keep\n" : path).getBytes(StandardCharsets.UTF_8));
                out.closeEntry();
            }
        }
        return zip;
    }

    @Test public void syntheticArchivesMapEachGameAndProtectSharedBase() throws Exception {
        for (String id : new String[] { "ettu", "sonora", "resurrection" }) {
            File top = temp.newFolder();
            new File(top, "base/fallout1").mkdirs();
            Files.write(new File(top, "master.dat").toPath(), new byte[] {42});
            Files.write(new File(top, "critter.dat").toPath(), new byte[] {43});
            String p = id.equals("ettu") ? "Fallout1in2/" : id.equals("sonora") ? "Fallout_Sonora_1_16_E/" : "";
            GameInstall.extract(top, id, zip(p + "mods/game.dat", p + "mods/mods_order.txt", p + "data/maps/test.map",
                p + "master.dat", p + "critter.dat", p + "Fallout2.cfg", p + "Fallout2.exe"), (d, t) -> assertTrue(d <= t));
            File root = GameInstall.target(top, id);
            assertEquals(new File(top, "games/" + id), root);
            for (String file : new String[] {"mods/game.dat", "mods/mods_order.txt", "data/maps/test.map"}) assertTrue(new File(root, file).isFile());
            for (String file : new String[] {"master.dat", "critter.dat", "fallout2.exe"}) assertFalse(new File(root, file).exists());
            assertEquals(42, Files.readAllBytes(new File(top, "master.dat").toPath())[0]);
            assertEquals(43, Files.readAllBytes(new File(top, "critter.dat").toPath())[0]);
            String cfg = new String(Files.readAllBytes(new File(root, "fallout2.cfg").toPath()), java.nio.charset.StandardCharsets.UTF_8);
            assertTrue(cfg.contains("master_dat=master.dat"));
            assertTrue(cfg.contains("value=keep"));
            assertFalse(cfg.contains("C:"));
            assertTrue(GameInstall.installed(top, id));
            assertFalse(new File(top, "fallout2.cfg").exists());
        }
    }

    @Test public void missingFallout1AndExistingRootsAreUntouched() throws Exception {
        File top = temp.newFolder();
        fails(() -> GameInstall.extract(top, "ettu", zip("Fallout1in2/data/test.dat"), (d, t) -> {}));
        File dest = GameInstall.target(top, "sonora");
        dest.mkdirs();
        File keep = new File(dest, "keep");
        Files.write(keep.toPath(), "keep".getBytes(java.nio.charset.StandardCharsets.UTF_8));
        fails(() -> GameInstall.extract(top, "sonora", zip("mods/test.dat"), (d, t) -> {}));
        assertEquals("keep", new String(Files.readAllBytes(keep.toPath()), java.nio.charset.StandardCharsets.UTF_8));
        fails(() -> GameInstall.target(top, "fallout2"));
        fails(() -> GameInstall.target(top, "../fallout2"));
    }

    @Test public void traversalAndFailedExtractionNeverPublish() throws Exception {
        File top = temp.newFolder();
        fails(() -> GameInstall.extract(top, "sonora", zip("../master.dat"), (d, t) -> {}));
        fails(() -> GameInstall.extract(top, "resurrection", zip("data/test.dat"), (d, t) -> { throw new IOException("stop"); }));
        assertFalse(GameInstall.target(top, "resurrection").exists());
        for (File f : new File(top, "games").listFiles()) assertFalse(f.getName().startsWith(".install-resurrection-"));
        fails(() -> GameInstall.mapped("sonora", "C:/data/a"));
        fails(() -> GameInstall.mapped("sonora", "data/../../master.dat"));
    }

    @Test public void symlinkTargetsAndCaseCollisionsAreRejected() throws Exception {
        File top = temp.newFolder(), outside = temp.newFolder();
        Files.createSymbolicLink(new File(top, "games").toPath(), outside.toPath());
        fails(() -> GameInstall.target(top, "sonora"));
        File clean = temp.newFolder();
        fails(() -> GameInstall.extract(clean, "sonora", zip("mods/A.dat", "mods/a.dat"), (d, t) -> {}));
        assertFalse(GameInstall.target(clean, "sonora").exists());
    }

    interface Action { void run() throws Exception; }
    private void fails(Action action) throws Exception {
        try { action.run(); fail("Expected IOException"); } catch (IOException expected) { }
    }
}
