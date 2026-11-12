package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import static org.junit.Assert.*;

public class RpuReleaseTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    // The real v2.4.34 asset list (trimmed to the fields used).
    private static final String RELEASE = ""
        + "{\n"
        + " \"tag_name\": \"v2.4.34\",\n"
        + " \"assets\": [\n"
        + "  {\n"
        + "   \"name\": \"rpu_v2.4.34.exe\",\n"
        + "   \"size\": 980251749,\n"
        + "   \"browser_download_url\": \"https://github.com/BGforgeNet/Fallout2_Restoration_Project/releases/download/v2.4.34/rpu_v2.4.34.exe\"\n"
        + "  },\n"
        + "  {\n"
        + "   \"name\": \"rpu_v2.4.34.exe.torrent\",\n"
        + "   \"size\": 19834,\n"
        + "   \"browser_download_url\": \"https://github.com/BGforgeNet/Fallout2_Restoration_Project/releases/download/v2.4.34/rpu_v2.4.34.exe.torrent\"\n"
        + "  },\n"
        + "  {\n"
        + "   \"name\": \"rpu_v2.4.34.zip\",\n"
        + "   \"size\": 866970542,\n"
        + "   \"browser_download_url\": \"https://github.com/BGforgeNet/Fallout2_Restoration_Project/releases/download/v2.4.34/rpu_v2.4.34.zip\"\n"
        + "  },\n"
        + "  {\n"
        + "   \"name\": \"rpu_v2.4.34.zip.torrent\",\n"
        + "   \"size\": 17674,\n"
        + "   \"browser_download_url\": \"https://github.com/BGforgeNet/Fallout2_Restoration_Project/releases/download/v2.4.34/rpu_v2.4.34.zip.torrent\"\n"
        + "  },\n"
        + "  {\n"
        + "   \"name\": \"rpu_v2.4.34_translations.zip\",\n"
        + "   \"size\": 140656143,\n"
        + "   \"browser_download_url\": \"https://github.com/BGforgeNet/Fallout2_Restoration_Project/releases/download/v2.4.34/rpu_v2.4.34_translations.zip\"\n"
        + "  },\n"
        + "  {\n"
        + "   \"name\": \"rpu_v2.4.34_translations.zip.torrent\",\n"
        + "   \"size\": 22639,\n"
        + "   \"browser_download_url\": \"https://github.com/BGforgeNet/Fallout2_Restoration_Project/releases/download/v2.4.34/rpu_v2.4.34_translations.zip.torrent\"\n"
        + "  }\n"
        + " ]\n"
        + "}\n";

    private static final ModInstall.Progress PROGRESS = new ModInstall.Progress() {
        public void update(long done, long total, String path) { }
        public boolean isCancelled() { return false; }
    };

    @Test public void picksTheGameZipNotTranslationsExeOrTorrent() throws IOException {
        RpuRelease release = RpuRelease.parse(RELEASE);
        assertEquals("rpu_v2.4.34.zip", release.zip.name);
        assertEquals(866970542L, release.zip.size);
        assertTrue(release.zip.url.endsWith("/v2.4.34/rpu_v2.4.34.zip"));
        assertEquals("2.4.34", release.version());
        assertEquals("Restoration Project 2.4.34", release.modName());
        assertEquals("rpu-v2.4.34-866970542.zip.part", release.partName());
    }

    @Test public void noZipMeansNull() throws IOException {
        assertNull(RpuRelease.parse("{\"tag_name\":\"v1\",\"assets\":[{\"name\":\"a.exe\",\"size\":5,\"browser_download_url\":\"https://x/a.exe\"}]}"));
    }

    @Test(expected = IOException.class) public void badJsonIsAnIoError() throws IOException {
        RpuRelease.parse("<html>rate limited</html>");
    }

    @Test public void jsonEscapesAndNesting() throws IOException {
        RpuRelease release = RpuRelease.parse("{\"x\":{\"a\":[1,2,{\"b\":null}]},\"tag_name\":\"v9\\u0031\","
            + "\"assets\":[{\"name\":\"rpu_x.zip\",\"size\":1.0e3,\"browser_download_url\":\"https://h/a\\/b.zip\",\"t\":true}]}");
        assertEquals("v91", release.tag);
        assertEquals(1000, release.zip.size);
        assertEquals("https://h/a/b.zip", release.zip.url);
    }

    @Test public void rangeHeaderAndResumeDecision() {
        assertNull(RpuRelease.rangeHeader(0, 100));
        assertEquals("bytes=40-", RpuRelease.rangeHeader(40, 100));
        assertNull(RpuRelease.rangeHeader(100, 100));
        assertNull(RpuRelease.rangeHeader(120, 100));
        assertEquals(RpuRelease.Resume.RESTART, RpuRelease.resume(0, 100));
        assertEquals(RpuRelease.Resume.RESUME, RpuRelease.resume(1, 100));
        assertEquals(RpuRelease.Resume.DONE, RpuRelease.resume(100, 100));
        assertEquals(RpuRelease.Resume.RESTART, RpuRelease.resume(101, 100));
    }

    @Test public void contentRangeMustMatchTheRequest() {
        assertTrue(RpuRelease.rangeAccepted(206, "bytes 40-99/100", 40, 100));
        assertFalse(RpuRelease.rangeAccepted(200, null, 40, 100));
        assertFalse(RpuRelease.rangeAccepted(206, null, 40, 100));
        assertFalse(RpuRelease.rangeAccepted(206, "bytes 41-99/100", 40, 100));
        assertFalse(RpuRelease.rangeAccepted(206, "bytes 40-99/101", 40, 100));
        assertFalse(RpuRelease.rangeAccepted(206, "bytes 40-98/100", 40, 100));
        assertFalse(RpuRelease.rangeAccepted(206, "items 40-99/100", 40, 100));
        assertFalse(RpuRelease.rangeAccepted(206, "bytes 40-99/*", 40, 100));
    }

    @Test public void freeSpaceCheck() {
        long total = 866970542L;
        long needed = RpuRelease.neededBytes(total, 0);
        assertTrue(needed > 2 * total);
        assertFalse(RpuRelease.enoughSpace(needed - 1, total, 0));
        assertTrue(RpuRelease.enoughSpace(needed, total, 0));
        // A partial file needs that much less.
        assertEquals(needed - 500, RpuRelease.neededBytes(total, 500));
        assertEquals(needed - total, RpuRelease.neededBytes(total, total + 7));
        assertEquals(827, RpuRelease.toMegabytes(total));
        assertEquals(1, RpuRelease.toMegabytes(1));
    }

    @Test public void wifiDecision() {
        assertFalse(RpuRelease.warnNotWifi(true, true));
        assertTrue(RpuRelease.warnNotWifi(true, false));
        assertTrue(RpuRelease.warnNotWifi(false, true));
        assertTrue(RpuRelease.warnNotWifi(false, false));
    }

    // The real zip's layout: data/, mods/*.dat + ini files, appearance/*.dat and
    // Windows files at the top, no wrapper folder.
    @Test public void realLayoutInstallsIntoTheStoreAndSkipsWindowsFiles() throws IOException {
        GameFilesTest.MemorySource zip = new GameFilesTest.MemorySource()
            .put("data/sound/music/01hub.acm", "m").put("mods/rpu.dat", "d").put("mods/rpu_extended_flamer.dat", "d")
            .put("mods/rpu.ini", "i").put("appearance/hfr00s00.dat", "a").put("ddraw.dll", "w").put("ddraw.ini", "i")
            .put("fallout2.exe", "w").put("f2_res.dll", "w").put("sfall.dat", "s").put("mods_order.txt", "o")
            .put("translations/german.ini", "t");
        File game = temporary.newFolder();
        assertTrue(new File(game, "profiles/p").mkdirs());
        Files.write(new File(game, "profiles/active.txt").toPath(), "p".getBytes());
        ModInstall.Result result = ModInstall.install(zip, "Restoration Project 2.4.34", game, PROGRESS);
        assertEquals("restoration-project-2.4.34", result.id);
        File mod = new File(game, "modstore/" + result.id);
        assertTrue(new File(mod, "data/sound/music/01hub.acm").isFile());
        assertTrue(new File(mod, "rpu.dat").isFile());
        assertTrue(new File(mod, "rpu_extended_flamer.dat").isFile());
        assertFalse(new File(mod, "ddraw.dll").exists());
        assertFalse(new File(mod, "fallout2.exe").exists());
        assertTrue(result.skipped.contains("ddraw.dll"));
        assertTrue(result.skipped.contains("fallout2.exe"));
        assertTrue(new String(Files.readAllBytes(new File(game, "profiles/p/modlist.txt").toPath())).contains("+" + result.id));
    }
}
