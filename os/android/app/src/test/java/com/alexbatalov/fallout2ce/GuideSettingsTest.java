package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class GuideSettingsTest {
    @Rule
    public TemporaryFolder folder = new TemporaryFolder();

    @Test
    public void setsKeysInTouchSectionKeepingTheRest() throws Exception {
        File game = folder.newFolder();
        Files.write(new File(game, "fallout2.cfg").toPath(),
            "[system]\nlanguage=english\n\n[touch]\nbottom_theme=0\n\n[ui]\nx=1\n".getBytes(StandardCharsets.ISO_8859_1));
        GuideSettings.setInt(game, "bottom_theme", 1);
        GuideSettings.setInt(game, "dashboard_layout", 2);
        assertEquals(1, GuideSettings.getInt(game, "bottom_theme", 0));
        assertEquals(2, GuideSettings.getInt(game, "dashboard_layout", 0));
        String text = new String(Files.readAllBytes(new File(game, "fallout2.cfg").toPath()), StandardCharsets.ISO_8859_1);
        assertEquals("[system]\nlanguage=english\n\n[touch]\nbottom_theme=1\ndashboard_layout=2\n\n[ui]\nx=1\n", text);
    }

    @Test
    public void createsSectionAndMarksDone() throws Exception {
        File game = folder.newFolder();
        assertEquals(7, GuideSettings.getInt(game, "bottom_theme", 7));
        GuideSettings.setInt(game, "bottom_theme", 1);
        assertEquals(1, GuideSettings.getInt(game, "bottom_theme", 0));
        assertFalse(GuideSettings.isDone(game));
        GuideSettings.markDone(game);
        assertTrue(GuideSettings.isDone(game));
        GuideSettings.clearDone(game);
        assertFalse(GuideSettings.isDone(game));
    }
}
