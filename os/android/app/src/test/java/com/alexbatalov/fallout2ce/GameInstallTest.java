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
            String p = id.equals("ettu") ? "Fallout1in2/" : id.equals("sonora") ? "Fallout_Sonora_1_16_E/" : "Resurrection/";
            GameInstall.extract(top, id, zip(p + "mods/game.dat", p + "mods/mods_order.txt", p + "data/maps/test.map",
                p + "master.dat", p + "critter.dat", p + "Fallout2.cfg", p + "Fallout2.exe"), (d, t) -> assertTrue(d <= t));
            File root = GameInstall.target(top, id);
            assertEquals(new File(top, "games/" + id), root);
            for (String file : new String[] {"mods/game.dat", "mods/mods_order.txt", "data/maps/test.map"}) assertTrue(new File(root, file).isFile());
            assertFalse(new File(root, "fallout2.exe").exists());
            // Sonora ships its own dats and keeps them; Et Tu and Resurrection use the player's.
            for (String file : new String[] {"master.dat", "critter.dat"}) assertEquals(id.equals("sonora"), new File(root, file).exists());
            assertEquals(42, Files.readAllBytes(new File(top, "master.dat").toPath())[0]);
            assertEquals(43, Files.readAllBytes(new File(top, "critter.dat").toPath())[0]);
            String cfg = new String(Files.readAllBytes(new File(root, "fallout2.cfg").toPath()), java.nio.charset.StandardCharsets.UTF_8);
            assertTrue(cfg.contains("master_dat=master.dat"));
            assertTrue(cfg.contains("value=keep"));
            assertFalse(cfg.contains("C:"));
            assertTrue(GameInstall.installed(top, id));
            assertFalse(new File(top, "fallout2.cfg").exists());
            org.junit.Assume.assumeTrue(root.toPath().getFileSystem().supportedFileAttributeViews().contains("posix"));
            assertTrue(Files.getPosixFilePermissions(root.toPath()).contains(java.nio.file.attribute.PosixFilePermission.GROUP_READ));
        }
    }

    @Test public void perGameDatRule() throws Exception {
        assertTrue(GameInstall.ownDats("sonora"));
        assertFalse(GameInstall.ownDats("ettu"));
        assertFalse(GameInstall.ownDats("resurrection"));
        assertEquals("master.dat", GameInstall.mapped("sonora", "Fallout_Sonora_1_16_E/Master.DAT"));
        assertEquals("critter.dat", GameInstall.mapped("sonora", "Fallout_Sonora_1_16_E/critter.dat"));
        assertNull(GameInstall.mapped("ettu", "Fallout1in2/master.dat"));
        assertNull(GameInstall.mapped("resurrection", "Resurrection/master.dat"));
        assertNull(GameInstall.mapped("sonora", "Fallout_Sonora_1_16_E/fallout2.exe"));
    }

    @Test public void resurrectionRealArchiveLayoutMapsToGameRoot() throws Exception {
        assertEquals("data/art/cuts/intro.mve", GameInstall.mapped("resurrection", "Resurrection/data/art/cuts/intro.mve"));
        assertEquals("sound/music/06vault.acm", GameInstall.mapped("resurrection", "sound/music/06VAULT.ACM"));
        assertEquals("mods/fores.dat", GameInstall.mapped("resurrection", "Resurrection/fores.dat"));
        assertEquals("mods/fores_critter.dat", GameInstall.mapped("resurrection", "Resurrection/fores_critter.dat"));
        assertEquals("ddraw.ini", GameInstall.mapped("resurrection", "Resurrection/ddraw.ini"));
        assertNull(GameInstall.mapped("resurrection", "Resurrection/fores.exe"));
        assertNull(GameInstall.mapped("resurrection", "Resurrection/FALLOUT2"));
        assertNull(GameInstall.mapped("resurrection", "Resurrection/fores.cfg"));
        assertNull(GameInstall.mapped("resurrection", "Resurrection/fores_readme.txt"));
        assertNull(GameInstall.mapped("resurrection", "Resurrection/ddraw.dll"));
    }

    @Test public void sonoraWithoutDatsIsIncompleteAndRepairKeepsPlayerFiles() throws Exception {
        File top = temp.newFolder();
        File root = GameInstall.target(top, "sonora");
        // An install made by the old installer: marker present, dats dropped.
        assertTrue(new File(root, "data/SAVEGAME/SLOT01").mkdirs());
        assertTrue(new File(root, "mods").mkdirs());
        Files.write(new File(root, ".installed").toPath(), "sonora\n".getBytes(StandardCharsets.UTF_8));
        Files.write(new File(root, "data/SAVEGAME/SLOT01/SAVE.DAT").toPath(), "save".getBytes(StandardCharsets.UTF_8));
        Files.write(new File(root, "mods/mine.dat").toPath(), "mine".getBytes(StandardCharsets.UTF_8));
        Files.write(new File(root, "fallout2.cfg").toPath(), "[system]\nmaster_dat=master.dat\n[mine]\nx=1\n".getBytes(StandardCharsets.UTF_8));
        assertFalse(GameInstall.installed(top, "sonora"));
        assertTrue(GameInstall.needsRepair(top, "sonora"));
        GameInstall.extract(top, "sonora", zip("Fallout_Sonora_1_16_E/master.dat", "Fallout_Sonora_1_16_E/critter.dat",
            "Fallout_Sonora_1_16_E/data/new.txt", "Fallout_Sonora_1_16_E/fallout2.cfg"), (d, t) -> {});
        assertTrue(GameInstall.installed(top, "sonora"));
        assertFalse(GameInstall.needsRepair(top, "sonora"));
        assertTrue(new File(root, "master.dat").isFile() && new File(root, "critter.dat").isFile());
        assertTrue(new File(root, "data/new.txt").isFile());
        assertEquals("save", new String(Files.readAllBytes(new File(root, "data/SAVEGAME/SLOT01/SAVE.DAT").toPath()), StandardCharsets.UTF_8));
        assertEquals("mine", new String(Files.readAllBytes(new File(root, "mods/mine.dat").toPath()), StandardCharsets.UTF_8));
        assertTrue(new String(Files.readAllBytes(new File(root, "fallout2.cfg").toPath()), StandardCharsets.UTF_8).contains("[mine]"));
        String[] left = root.list((d, n) -> n.endsWith(".repair-part"));
        assertEquals(0, left.length);
    }

    @Test public void gamesWithoutOwnDatsAreNeverIncomplete() throws Exception {
        File top = temp.newFolder();
        File root = GameInstall.target(top, "resurrection");
        assertTrue(root.mkdirs());
        Files.write(new File(root, ".installed").toPath(), "resurrection\n".getBytes(StandardCharsets.UTF_8));
        assertTrue(GameInstall.installed(top, "resurrection"));
        assertFalse(GameInstall.needsRepair(top, "resurrection"));
        File none = temp.newFolder();
        assertFalse(GameInstall.needsRepair(none, "sonora")); // not installed at all
        assertFalse(GameInstall.installed(none, "sonora"));
    }

    @Test public void shareTreeAddsGroupAccessToOwnerOnlyTree() throws Exception {
        org.junit.Assume.assumeTrue(temp.getRoot().toPath().getFileSystem().supportedFileAttributeViews().contains("posix"));
        File top = temp.newFolder();
        File game = new File(top, "games/x");
        File sub = new File(game, "data/maps");
        assertTrue(sub.mkdirs());
        File f = new File(sub, "a.map");
        Files.write(f.toPath(), new byte[] {1});
        java.util.Set<java.nio.file.attribute.PosixFilePermission> own = java.nio.file.attribute.PosixFilePermissions.fromString("rwx------");
        for (File d : new File[] {sub, sub.getParentFile(), game}) Files.setPosixFilePermissions(d.toPath(), own);
        Files.setPosixFilePermissions(f.toPath(), java.nio.file.attribute.PosixFilePermissions.fromString("rw-------"));
        GameInstall.shareGames(top);
        java.nio.file.attribute.PosixFilePermission gr = java.nio.file.attribute.PosixFilePermission.GROUP_READ;
        assertTrue(Files.getPosixFilePermissions(game.toPath()).contains(gr));
        assertTrue(Files.getPosixFilePermissions(sub.toPath()).contains(java.nio.file.attribute.PosixFilePermission.GROUP_EXECUTE));
        assertTrue(Files.getPosixFilePermissions(f.toPath()).contains(gr));
    }

    @Test public void shareGamesRepairsNestedFileUnderAlreadySharedDirectories() throws Exception {
        org.junit.Assume.assumeTrue(temp.getRoot().toPath().getFileSystem().supportedFileAttributeViews().contains("posix"));
        File top = temp.newFolder();
        File game = new File(top, "games/x");
        File sub = new File(game, "data/maps");
        assertTrue(sub.mkdirs());
        File file = new File(sub, "a.map");
        Files.write(file.toPath(), new byte[] {1});
        for (File dir : new File[] {game, sub.getParentFile(), sub}) {
            Files.setPosixFilePermissions(dir.toPath(), java.nio.file.attribute.PosixFilePermissions.fromString("rwxrwxr-x"));
        }
        Files.setPosixFilePermissions(file.toPath(), java.nio.file.attribute.PosixFilePermissions.fromString("rw-------"));
        assertFalse(Files.getPosixFilePermissions(file.toPath()).contains(java.nio.file.attribute.PosixFilePermission.OTHERS_READ));

        GameInstall.shareGames(top);

        assertTrue(Files.getPosixFilePermissions(file.toPath()).contains(java.nio.file.attribute.PosixFilePermission.OTHERS_READ));
        assertFalse(Files.getPosixFilePermissions(file.toPath()).contains(java.nio.file.attribute.PosixFilePermission.OTHERS_EXECUTE));
        // A later owner-only directory must also be repaired on the next start.
        Files.setPosixFilePermissions(sub.toPath(), java.nio.file.attribute.PosixFilePermissions.fromString("rwx------"));
        GameInstall.shareGames(top);
        for (File dir : new File[] {game, sub.getParentFile(), sub}) {
            assertTrue(Files.getPosixFilePermissions(dir.toPath()).contains(java.nio.file.attribute.PosixFilePermission.OTHERS_READ));
            assertTrue(Files.getPosixFilePermissions(dir.toPath()).contains(java.nio.file.attribute.PosixFilePermission.OTHERS_EXECUTE));
        }
    }

    @Test public void shareGamesDoesNotFollowSymlinks() throws Exception {
        org.junit.Assume.assumeTrue(temp.getRoot().toPath().getFileSystem().supportedFileAttributeViews().contains("posix"));
        File top = temp.newFolder();
        File game = new File(top, "games/x");
        assertTrue(game.mkdirs());
        File outside = temp.newFolder();
        File file = new File(outside, "private.dat");
        Files.write(file.toPath(), new byte[] {1});
        java.util.Set<java.nio.file.attribute.PosixFilePermission> dirPerms = java.nio.file.attribute.PosixFilePermissions.fromString("rwx------");
        java.util.Set<java.nio.file.attribute.PosixFilePermission> filePerms = java.nio.file.attribute.PosixFilePermissions.fromString("rw-------");
        Files.setPosixFilePermissions(outside.toPath(), dirPerms);
        Files.setPosixFilePermissions(file.toPath(), filePerms);
        Files.createSymbolicLink(new File(game, "linked-dir").toPath(), outside.toPath());
        Files.createSymbolicLink(new File(game, "linked-file").toPath(), file.toPath());
        Files.createSymbolicLink(new File(top, "games/linked-game").toPath(), outside.toPath());

        GameInstall.shareGames(top);

        assertEquals(dirPerms, Files.getPosixFilePermissions(outside.toPath()));
        assertEquals(filePerms, Files.getPosixFilePermissions(file.toPath()));
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
        fails(() -> GameInstall.extract(top, "resurrection", zip("Resurrection/data/test.dat"), (d, t) -> { throw new IOException("stop"); }));
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
