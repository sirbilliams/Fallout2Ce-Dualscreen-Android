package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class GameRootsTest {
    @Rule public TemporaryFolder tmp = new TemporaryFolder();

    private void select(File top, String text) throws Exception {
        File f = new File(top, "profiles/last-game.txt");
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    @Test public void fallout2PathsAreTheTopRoot() throws Exception {
        File top = tmp.newFolder("top");
        assertEquals(top, GameRoots.gameRoot(top, "fallout2"));
        assertEquals(top.getPath(), GameRoots.gameRoot(top, "fallout2").getPath());
        assertEquals(top, GameRoots.baseRoot(top, "fallout2"));
        assertEquals("fallout2", GameRoots.activeGameId(top)); // no selection file
    }

    @Test public void otherGamesLiveUnderGames() throws Exception {
        File top = tmp.newFolder("top");
        assertEquals(new File(top, "games/sonora"), GameRoots.gameRoot(top, "sonora"));
        assertEquals(new File(top, "base/fallout1"), GameRoots.baseRoot(top, "fallout1"));
    }

    @Test public void selectionParsing() throws Exception {
        File top = tmp.newFolder("top");
        select(top, "ettu\n");
        assertEquals("ettu", GameRoots.activeGameId(top));
        select(top, "nevada");
        assertEquals("nevada", GameRoots.activeGameId(top));
        select(top, "");
        assertEquals("fallout2", GameRoots.activeGameId(top));
    }

    @Test public void unknownIdFallsBackToFallout2() throws Exception {
        File top = tmp.newFolder("top");
        select(top, "nonsense\n");
        assertEquals("fallout2", GameRoots.activeGameId(top));
        assertEquals(top, GameRoots.gameRoot(top, "nonsense"));
        assertEquals(top, GameRoots.gameRoot(top, null));
        assertEquals("fallout2", GameRoots.activeGameId((File) null));
        assertNull(GameRoots.gameRoot((File) null, "ettu"));
    }

    @Test public void idsMatchNativeCatalog() throws Exception {
        Path src = null;
        for (Path dir = new File("").getAbsoluteFile().toPath(); dir != null; dir = dir.getParent()) {
            Path candidate = dir.resolve("src/game_catalog.cc");
            if (Files.isRegularFile(candidate)) { src = candidate; break; }
        }
        List<String> expected = new ArrayList<>();
        if (src == null) {
            // Source tree not reachable from the test's working directory: hard-coded copy.
            expected.addAll(Arrays.asList("fallout2", "ettu", "sonora", "resurrection", "nevada", "olympus2207", "megamod", "yesterday"));
        } else {
            Matcher m = Pattern.compile("^\\s*\\{ \"([a-z0-9]+)\", \"", Pattern.MULTILINE)
                .matcher(new String(Files.readAllBytes(src), StandardCharsets.UTF_8));
            while (m.find()) expected.add(m.group(1));
        }
        assertEquals(expected, Arrays.asList(GameRoots.IDS));
    }
}
