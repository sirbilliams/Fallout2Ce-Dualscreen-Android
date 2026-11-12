package com.alexbatalov.fallout2ce;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import static org.junit.Assert.*;

// Per-profile HD settings: profiles/<name>/hd.ini over fallout2.cfg.
public class HdProfileSettingsTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();
    private File game;

    @Before public void setUp() throws IOException {
        game = temporary.newFolder("game");
        write("fallout2.cfg", "[system]\nlanguage=english\n[hd]\nenabled=0\n");
        new File(game, "profiles/A").mkdirs();
        new File(game, "profiles/B").mkdirs();
    }

    private void write(String path, String text) throws IOException {
        File file = new File(game, path);
        file.getParentFile().mkdirs();
        Files.write(file.toPath(), text.getBytes(StandardCharsets.ISO_8859_1));
    }

    private String read(String path) throws IOException {
        return new String(Files.readAllBytes(new File(game, path).toPath()), StandardCharsets.ISO_8859_1);
    }

    private void activate(String name) throws IOException {
        write("profiles/active.txt", name + "\n");
    }

    @Test public void vanillaUsesFalloutCfgAndNoProfileFileIsMade() throws IOException {
        assertEquals("", HdSettings.activeProfile(game));
        HdSettings.setEnabled(game, true);
        assertTrue(HdSettings.isEnabled(game));
        assertTrue(read("fallout2.cfg").contains("enabled=1"));
        activate("Vanilla");
        assertEquals("", HdSettings.activeProfile(game));
        assertTrue(HdSettings.isEnabled(game));
        assertFalse(new File(game, "profiles/Vanilla").exists());
    }

    @Test public void profileKeepsItsOwnSwitchAndQualityAndLeavesTheConfigAlone() throws IOException {
        activate("A");
        assertEquals("A", HdSettings.activeProfile(game));
        String before = read("fallout2.cfg");
        HdSettings.setEnabled(game, true);
        HdSettings.setQuality(game, "medium");
        assertTrue(HdSettings.isEnabled(game));
        assertEquals("medium", HdSettings.quality(game));
        String ini = read("profiles/A/hd.ini");
        assertTrue(ini, ini.contains("[hd]\nenabled=1"));
        assertTrue(ini, ini.contains("mode=new") && ini.contains("quality=medium"));
        // hd_art_dir is global (no pack here, so nothing is written for it) and the
        // enabled flag in fallout2.cfg is untouched.
        assertEquals(before, read("fallout2.cfg"));

        // another profile and Vanilla do not see it
        activate("B");
        assertFalse(HdSettings.isEnabled(game));
        assertEquals("high", HdSettings.quality(game));
        activate("Vanilla");
        assertFalse(HdSettings.isEnabled(game));
        activate("A");
        HdSettings.setQuality(game, "off");
        assertEquals("off", HdSettings.quality(game));
        assertTrue(read("profiles/A/hd.ini").contains("mode=original"));
    }

    @Test public void profileOffOverridesGlobalOnAndMissingKeysFallBack() throws IOException {
        write("fallout2.cfg", "[hd]\nenabled=1\n[hd_light]\nquality=low\n");
        activate("A");
        assertTrue(HdSettings.isEnabled(game));              // no hd.ini: global
        assertEquals("low", HdSettings.quality(game));
        HdSettings.setEnabled(game, false);
        assertFalse(HdSettings.isEnabled(game));             // profile off, global on
        assertEquals("low", HdSettings.quality(game));       // quality key missing: global
        activate("B");
        assertTrue(HdSettings.isEnabled(game));
        assertTrue(read("fallout2.cfg").contains("enabled=1"));
    }

    @Test public void enablingWithAPackPointsTheGlobalArtDirAtItAndOnlyThatProfileTurnsOn() throws IOException {
        new File(game, "hd/2x").mkdirs();
        activate("A");
        HdSettings.setEnabled(game, true);
        assertTrue(read("fallout2.cfg").contains("hd_art_dir=hd/2x"));
        assertTrue(read("fallout2.cfg").contains("enabled=0"));
        assertTrue(HdSettings.isEnabled(game));
        activate("B");
        assertFalse(HdSettings.isEnabled(game));
    }

    @Test public void unknownOrUnsafeActiveNamesMeanVanilla() throws IOException {
        activate("Nope");
        assertEquals("", HdSettings.activeProfile(game));
        activate("../A");
        assertEquals("", HdSettings.activeProfile(game));
        activate("");
        assertEquals("", HdSettings.activeProfile(game));
    }

    @Test public void dustlightReadyTurnsOnTheActiveProfileOnly() throws IOException {
        activate("A");
        HdPackInstall.Progress none = new HdPackInstall.Progress() {
            public void update(long done, long total, String path) { }
            public boolean isCancelled() { return false; }
        };
        DustlightHd.Status before = DustlightHd.status(game);
        HdPackInstall.install(DustlightHdTest.p1(), DustlightHdTest.P1, game, none, false);
        HdPackInstall.install(DustlightHdTest.p2(), DustlightHdTest.P2, game, none, false);
        HdPackInstall.install(DustlightHdTest.p3(), DustlightHdTest.P3, game, none, false);
        assertTrue(DustlightHd.enableIfNewlyComplete(game, before));
        assertTrue(HdSettings.isEnabled(game));
        activate("B");
        assertFalse(HdSettings.isEnabled(game));
        activate("Vanilla");
        assertFalse(HdSettings.isEnabled(game));
    }
}
