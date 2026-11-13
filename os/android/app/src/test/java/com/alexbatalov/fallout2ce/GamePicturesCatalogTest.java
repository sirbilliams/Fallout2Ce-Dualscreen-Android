package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.Test;

public class GamePicturesCatalogTest {
    @Test public void installersNeverTargetFallout2() {
        assertFalse(GameInstall.supported("fallout2"));
        assertNull(GamePictures.catalogInfo("fallout2"));
    }

    @Test public void creatorAndSourceMatchNativeCatalog() throws Exception {
        Path src = null;
        for (Path dir = new File("").getAbsoluteFile().toPath(); dir != null; dir = dir.getParent()) {
            Path candidate = dir.resolve("src/game_catalog.cc");
            if (Files.isRegularFile(candidate)) { src = candidate; break; }
        }
        if (src == null) return; // source tree not reachable from the test's working directory
        String text = new String(Files.readAllBytes(src), StandardCharsets.UTF_8);
        for (String id : new String[] { "ettu", "sonora", "resurrection" }) {
            Matcher m = Pattern.compile("\\{ \"" + id + "\", \"[^\"]*\", \"([^\"]*)\".*\"([^\"]*)\" \\},").matcher(text);
            if (!m.find()) throw new AssertionError("catalog row missing: " + id);
            String[] info = GamePictures.catalogInfo(id);
            assertEquals(m.group(1), info[0]);
            assertEquals(m.group(2), info[1]);
        }
    }
}
