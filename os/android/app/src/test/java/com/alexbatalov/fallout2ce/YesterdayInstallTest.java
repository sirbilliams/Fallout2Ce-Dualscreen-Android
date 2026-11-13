package com.alexbatalov.fallout2ce;

import org.junit.Test;
import org.junit.Rule;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.zip.*;
import static org.junit.Assert.*;

public class YesterdayInstallTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();
    private File base() throws Exception {
        File top = temp.newFolder();
        for (String f : new String[] { "master.dat", "critter.dat", "patch000.dat" })
            Files.write(new File(top, f).toPath(), ("player " + f).getBytes(StandardCharsets.UTF_8));
        return top;
    }
    private Map<String, String> layout() {
        Map<String, String> f = new LinkedHashMap<>();
        f.put("fallouty.cfg", "[system]\nmaster_dat=C:\\master.dat\ncritter_dat=C:\\critter.dat\nmaster_patches=data\\\n[sound]\nmusic_path1=data\\sound\\music\\\n[preferences]\ngame_difficulty=2\n");
        f.put("ddraw.ini", "[Misc]\nConfigFile=fallouty.cfg\nStartingMap=tbcb13.map\nPatchFile=patch%03d.dat\nStartYear=2253\nMovie1=TITLE.MVE\n[Other]\nConfigFile=keep\n");
        f.put("patch001.dat/Scripts/start.int", "loose scripts");
        f.put("patch002.dat", "DAT2 fixture");
        f.put("data/maps/start.map", "loose override");
        f.put("appearance/hmr/test.frm", "art");
        f.put("sfall.dat", "sfall art");
        f.put("mods/mods_order.txt", "");
        return f;
    }
    private File zip(String prefix, Map<String, String> files) throws Exception {
        File f = temp.newFile();
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(f))) {
            for (Map.Entry<String, String> e : files.entrySet()) {
                out.putNextEntry(new ZipEntry(prefix + e.getKey()));
                out.write(e.getValue().getBytes(StandardCharsets.UTF_8));
                out.closeEntry();
            }
        }
        return f;
    }
    private String read(File root, String f) throws Exception {
        return new String(Files.readAllBytes(new File(root, f).toPath()), StandardCharsets.UTF_8);
    }
    @Test public void layoutConfigAndSharedBase() throws Exception {
        File top = base();
        GameInstall.extract(top, "yesterday", zip("fallouty-main/", layout()), (d,t) -> assertTrue(d <= t));
        File root = GameInstall.target(top, "yesterday");
        assertEquals(new File(top, "games/yesterday"), root);
        assertTrue(GameInstall.installed(top, "yesterday"));
        assertFalse(GameInstall.ownDats("yesterday"));
        assertEquals("loose scripts", read(root, "patch001.dat/scripts/start.int"));
        assertEquals("loose override", read(root, "data/maps/start.map"));
        assertEquals("art", read(root, "appearance/hmr/test.frm"));
        assertEquals("sfall art", read(root, "mods/sfall.dat"));
        assertTrue(read(root, "mods/mods_order.txt").contains("sfall.dat"));
        assertEquals("player patch000.dat", read(top, "patch000.dat"));
        assertFalse(new File(root, "master.dat").exists());
        assertFalse(new File(top, "fallout2.cfg").exists());
        String cfg = read(root, "fallout2.cfg");
        assertEquals(cfg, read(root, "fallouty.cfg"));
        assertTrue(cfg.contains("master_dat=master.dat"));
        assertTrue(cfg.contains("music_path1=data/sound/music/"));
        assertTrue(cfg.contains("game_difficulty=2"));
        assertFalse(cfg.contains("C:"));
        String ini = read(root, "ddraw.ini");
        assertTrue(ini.contains("ConfigFile=fallout2.cfg"));
        assertFalse(ini.contains("ConfigFile=fallouty.cfg"));
        for (String key : new String[] { "StartingMap=tbcb13.map", "PatchFile=patch%03d.dat", "StartYear=2253", "Movie1=TITLE.MVE", "ConfigFile=keep" }) assertTrue(ini.contains(key));
    }
    @Test public void extractionFiltersWindowsToolsAndBaseDats() throws Exception {
        File top = base(); Map<String,String> files = layout();
        for (String path : new String[] { "Fallout2.EXE", "ddraw.DLL", "RUN.BAT", "_tools/DAT/example.dat", "master.dat", "critter.dat", "patch000.dat", ".installed" }) {
            files.put(path, "untrusted");
            assertNull(GameInstall.mapped("yesterday", "fallouty-main/" + path));
        }
        GameInstall.extract(top, "yesterday", zip("fallouty-main/", files), (d,t) -> {});
        File root = GameInstall.target(top, "yesterday");
        assertFalse(new File(root, "_tools").exists());
        assertFalse(new File(root, "patch000.dat").exists());
        assertEquals("player master.dat", read(top, "master.dat"));
    }
    @Test public void pointerStubsAreSkippedAndLogged() throws Exception {
        File top = base(); Map<String,String> files = layout();
        files.put("data/art/cuts/TITLE.MVE", YesterdayInstall.LFS_HEADER + "\noid sha256:" + "0".repeat(64) + "\nsize 123\n");
        GameInstall.extract(top, "yesterday", zip("fallouty-main/", files), (d,t) -> {});
        File root = GameInstall.target(top, "yesterday");
        assertFalse(new File(root, "data/art/cuts/title.mve").exists());
        assertTrue(read(root, "yesterday-install.log").contains("Skipped Git LFS pointer: data/art/cuts/title.mve"));
        assertTrue(GameInstall.installed(top, "yesterday"));
    }
    @Test public void movingArchiveAcceptsNewSizeButRequiresExpectedLayout() throws Exception {
        File top = base(); File archive = zip("fallouty-main/", layout());
        assertNotEquals(YesterdayInstall.ARCHIVE_BYTES, archive.length());
        GameInstall.extract(top, "yesterday", archive, (d,t) -> {});
        assertTrue(GameInstall.installed(top, "yesterday"));
        File wrong = base();
        fails(() -> GameInstall.extract(wrong, "yesterday", zip("wrong/", layout()), (d,t) -> {}));
        assertFalse(GameInstall.target(wrong, "yesterday").exists());
        Map<String,String> files = layout(); files.remove("patch001.dat/Scripts/start.int");
        fails(() -> GameInstall.extract(wrong, "yesterday", zip("fallouty-main/", files), (d,t) -> {}));
    }
    @Test public void requiresAllThreePlayerFiles() throws Exception {
        File top = temp.newFolder();
        for (String f : new String[] { "master.dat", "critter.dat", "patch000.dat" }) {
            fails(() -> GameInstall.requireBase(top, "yesterday"));
            Files.write(new File(top, f).toPath(), new byte[] {1});
        }
        GameInstall.requireBase(top, "yesterday");
    }
    @Test public void preflightUsesUnpackedReportWithMargin() throws Exception {
        assertEquals(4481987989L + 10300000000L + 67108864L, YesterdayInstall.preflight());
        assertFalse(GameInstall.enoughSpace(YesterdayInstall.preflight()-1, YesterdayInstall.ARCHIVE_BYTES, YesterdayInstall.UNPACKED_BYTES));
    }
    @Test public void traversalAndCaseCollisionsFailWithoutPublishing() throws Exception {
        File top = base();
        fails(() -> GameInstall.mapped("yesterday", "fallouty-main/../escape"));
        fails(() -> GameInstall.mapped("yesterday", "fallouty-main/C:/escape"));
        Map<String,String> files = layout(); files.put("PATCH002.DAT", "collision");
        fails(() -> GameInstall.extract(top, "yesterday", zip("fallouty-main/", files), (d,t) -> {}));
        assertFalse(GameInstall.target(top, "yesterday").exists());
    }
    @Test public void interruptedAndInvalidArchivesNeverPublish() throws Exception {
        File top = base();
        fails(() -> GameInstall.extract(top, "yesterday", zip("fallouty-main/", layout()), (d,t) -> { throw new IOException("interrupted"); }));
        assertFalse(GameInstall.target(top, "yesterday").exists());
        fails(() -> GameInstall.extract(top, "yesterday", temp.newFile(), (d,t) -> {}));
        assertFalse(GameInstall.installed(top, "yesterday"));
    }
    interface Action { void run() throws Exception; }
    private void fails(Action action) throws Exception {
        try { action.run(); fail("Expected IOException"); } catch (IOException expected) { }
    }
}
