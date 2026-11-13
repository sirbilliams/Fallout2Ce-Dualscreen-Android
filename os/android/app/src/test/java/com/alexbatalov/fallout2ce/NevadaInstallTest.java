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

public class NevadaInstallTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();
    private static final String PREFIX = "Fallout-Nevada-English/";

    private File zip(String ini, String... extra) throws Exception {
        File zip = temp.newFile();
        Map<String, String> entries = new LinkedHashMap<>();
        for (String name : new String[] {"master.dat", "critter.dat", "f2_res.dat", "sfall.dat",
            "data/worldmap.dat", "data/art/test.frm", "data/sound/music/test.acm", "data/text/english/test.msg",
            "mods/KeysHelp.dat", "mods/mods_order.ini", "appearance/HMR00S00.dat", "sfall/scripts/gl_test.int"})
            entries.put(name, "data");
        entries.put("config.cfg", "[system]\nmaster_dat=C:\\Nevada\\master.dat\ncritter_dat=D:\\critter.dat\n"
            + "master_patches=C:\\data\ncritter_patches=D:\\data\nlanguage=russian\nart_cache_size=256\n"
            + "[preferences]\ngame_difficulty=2\n");
        if (ini != null) entries.put("ddraw.ini", ini);
        for (String name : extra) entries.put(name, "excluded");
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(zip))) {
            for (Map.Entry<String, String> entry : entries.entrySet()) {
                out.putNextEntry(new ZipEntry(PREFIX + entry.getKey()));
                out.write(entry.getValue().getBytes(StandardCharsets.ISO_8859_1));
                out.closeEntry();
            }
        }
        return zip;
    }

    private String read(File root, String name) throws Exception {
        return new String(Files.readAllBytes(new File(root, name).toPath()), StandardCharsets.ISO_8859_1);
    }

    @Test public void pinnedReleaseAndSpace() throws Exception {
        String json = "{\"assets\":[{\"name\":\"Fallout-Nevada-English.zip\",\"size\":1029036403,"
            + "\"browser_download_url\":\"" + GameInstall.NEVADA_URL + "\"}]}";
        assertEquals(GameInstall.NEVADA_SIZE, GameInstall.asset("nevada", json).size);
        assertEquals("HogLord69/fallout-english-localization", GameInstall.repo("nevada"));
        assertTrue(GameInstall.releaseApi("nevada").endsWith("/releases/tags/v1.0"));
        assertEquals(2500000000L, GameInstall.preflight("nevada", GameInstall.NEVADA_SIZE));
        fails(() -> GameInstall.asset("nevada", json.replace("1029036403", "123")));
        fails(() -> GameInstall.asset("nevada", json.replace("v1.0/", "v2.0/")));
        fails(() -> GameInstall.preflight("nevada", 123));
        fails(() -> GameInstall.verifyArchive("nevada", temp.newFile()));
    }

    @Test public void sha256RejectsTampering() throws Exception {
        File file = temp.newFile();
        Files.write(file.toPath(), "abc".getBytes(StandardCharsets.US_ASCII));
        String hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        GameInstall.verifySha256(file, hash);
        Files.write(file.toPath(), "abd".getBytes(StandardCharsets.US_ASCII));
        fails(() -> GameInstall.verifySha256(file, hash));
    }

    @Test public void extractsRuntimeLayoutWithOwnDatsAndSelectedConfig() throws Exception {
        File top = temp.newFolder();
        GameInstall.extract(top, "nevada", zip("[Misc]\nConfigFile=config.cfg\n",
            "NEVADA_EV.EXE", "ddraw.DLL", "Play Nevada.BAT", "game.ICO", "!docs/readme.txt",
            "screen/test.png", "data/tool.ExE", "sfall/tool.DlL", "mods/tool.BaT", "appearance/test.iCo"), (d, t) -> {});
        File root = new File(top, "games/nevada");
        assertTrue(GameInstall.installed(top, "nevada"));
        assertTrue(GameInstall.ownDats("nevada"));
        for (String file : new String[] {"master.dat", "critter.dat", "f2_res.dat", "sfall.dat", "data/worldmap.dat",
            "mods/keyshelp.dat", "mods/mods_order.ini", "appearance/hmr00s00.dat", "sfall/scripts/gl_test.int"})
            assertTrue(file, new File(root, file).isFile());
        for (String file : new String[] {"nevada_ev.exe", "ddraw.dll", "play nevada.bat", "game.ico", "!docs", "screen",
            "data/tool.exe", "sfall/tool.dll", "mods/tool.bat", "appearance/test.ico", "fallout2.cfg", "data/game.cfg"})
            assertFalse(file, new File(root, file).exists());
        String config = read(root, "config.cfg");
        for (String value : new String[] {"master_dat=master.dat", "critter_dat=critter.dat", "master_patches=data",
            "critter_patches=data", "language=english", "art_cache_size=256", "game_difficulty=2"}) assertTrue(config.contains(value));
        assertFalse(config.contains("C:"));
        assertFalse(config.contains("D:"));
        String ini = read(root, "ddraw.ini");
        for (String value : new String[] {"StartYear=2140", "StartMonth=9", "StartDay=10",
            "MovieTimer_artimer1=36000", "MovieTimer_artimer2=36030", "MovieTimer_artimer3=36060", "MovieTimer_artimer4=36090"})
            assertTrue(ini.contains(value));
    }

    @Test public void preservesExplicitShippedTimersAndAddsOnlyMissingMiscKeys() throws Exception {
        File top = temp.newFolder();
        GameInstall.extract(top, "nevada", zip("[Misc]\r\nConfigFile=config.cfg\r\n;StartYear=-1\r\n"
            + "StartMonth=8\r\nMovieTimer_artimer1=-1\r\nMovieTimer_artimer2=-1\r\nMovieTimer_artimer3=-1\r\nMovieTimer_artimer4=-1\r\n"
            + "[Other]\r\nStartYear=999\r\n"), (d, t) -> {});
        File root = GameInstall.target(top, "nevada");
        String ini = read(root, "ddraw.ini");
        assertTrue(ini.contains("StartYear=2140"));
        assertTrue(ini.contains("StartMonth=8"));
        assertTrue(ini.contains("StartYear=999"));
        for (int i = 1; i <= 4; i++) assertTrue(ini.contains("MovieTimer_artimer" + i + "=-1"));
        assertFalse(ini.contains("36000"));
        GameInstall.nevadaDefaults(root);
        assertEquals(ini, read(root, "ddraw.ini"));
    }

    @Test public void missingIniIsCreatedBeforeFirstLaunch() throws Exception {
        File top = temp.newFolder();
        GameInstall.extract(top, "nevada", zip(null), (d, t) -> {});
        assertTrue(read(GameInstall.target(top, "nevada"), "ddraw.ini").startsWith("[Misc]\nConfigFile=config.cfg\n"));
    }

    @Test public void rejectsTraversalAndAmbiguousPaths() throws Exception {
        assertEquals("appearance/hmr00s00.dat", GameInstall.mapped("nevada", PREFIX + "appearance/HMR00S00.DAT"));
        assertNull(GameInstall.mapped("nevada", "other/master.dat"));
        fails(() -> GameInstall.mapped("nevada", PREFIX + "../data/test"));
        fails(() -> GameInstall.mapped("nevada", PREFIX + "data/C:/test"));
        File top = temp.newFolder();
        fails(() -> GameInstall.extract(top, "nevada", zip(null, "MASTER.DAT"), (d, t) -> {}));
        assertFalse(GameInstall.target(top, "nevada").exists());
    }

    interface Action { void run() throws Exception; }
    private void fails(Action action) throws Exception {
        try { action.run(); fail("Expected IOException"); } catch (IOException expected) { }
    }
}
