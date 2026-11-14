package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import java.io.File;
import java.nio.file.Files;
import java.util.*;
import static org.junit.Assert.*;

public class PickerRankingTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    private long score(String name, String mime, int width, int height, double richness) {
        return PickerPictures.score("https://example.org/" + name, mime, width, height, richness, false);
    }

    @Test public void rejectsUnsuitableImages() {
        for (String name : new String[] { "logo", "badge", "banner", "icon", "button", "avatar", "title", "favicon", "shield" })
            assertEquals(name, -1, score(name + ".png", "image/png", 1920, 1080, 100));
        for (String host : new String[] { "opengraph.githubassets.com", "repository-images.githubusercontent.com" })
            assertEquals(-1, PickerPictures.score("https://" + host + "/a/b", "image/png", 1920, 1080, 100, false));
        assertEquals(-1, score("a.gif", "image/gif", 1920, 1080, 100));
        assertEquals(-1, score("a.png", "image/png", 599, 400, 100));
        assertEquals(-1, score("a.png", "image/png", 1920, 1600, 100));
        assertEquals(-1, score("a.png", "image/png", 1920, 800, 100));
        assertTrue(score("a.png", "image/png", 1300, 1000, 100) > 0);
        assertTrue(score("a.png", "image/png", 2200, 1000, 100) > 0);
    }

    @Test public void prefersAreaScreenshotsAndColourAndOnlineOverLocal() {
        long ordinary = score("a.png", "image/png", 1920, 1080, 100);
        assertTrue(score("screenshot.jpg", "image/jpeg", 1920, 1080, 100) > ordinary);
        assertTrue(score("a.png", "image/png", 1920, 1080, 0) < ordinary);
        assertTrue(score("a.png", "image/png", 3840, 2160, 100) > ordinary);
        assertTrue(PickerPictures.score("game-file:splash0.rix", "image/rix", 4096, 4096, 100, true)
            < score("a.png", "image/png", 960, 600, 0));
    }

    @Test public void wikiParsesPageImagesOriginalSizesAndMissingPages() {
        String page = "{\"query\":{\"pages\":{\"42\":{\"original\":{\"source\":\"https://example.org/cover.jpg\",\"width\":1920,\"height\":1080},\"images\":[{\"title\":\"File:Sonora screenshot.png\"}]}}}}";
        assertEquals(Arrays.asList("https://example.org/cover.jpg"), PickerPictures.wikiImages(page));
        assertTrue(PickerPictures.wikiImageInfoApi(page).contains("File%3ASonora+screenshot.png"));
        String info = "{\"query\":{\"pages\":{\"1\":{\"imageinfo\":[{\"url\":\"https://example.org/screen.png\",\"width\":1920,\"height\":1080,\"mime\":\"image/png\"}]},\"2\":{\"imageinfo\":[{\"url\":\"https://example.org/icon.png\",\"width\":64,\"height\":64,\"mime\":\"image/png\"}]}}}}";
        assertEquals(Arrays.asList("https://example.org/screen.png"), PickerPictures.wikiImages(info));
        assertTrue(PickerPictures.wikiImages("{\"query\":{\"pages\":{\"-1\":{\"missing\":\"\"}}}}").isEmpty());
        assertNull(PickerPictures.wikiImageInfoApi("{}"));
        for (String id : GameRoots.IDS) assertFalse(PickerPictures.wikiTitle(id).isEmpty());
    }

    @Test public void githubParsesOnlyImageFilesAndReleaseAssets() {
        String contents = "[{\"type\":\"file\",\"name\":\"screen.JPG\",\"download_url\":\"https://example.org/screen.JPG\"},{\"type\":\"dir\",\"name\":\"docs\",\"download_url\":null},{\"type\":\"file\",\"name\":\"logo.png\",\"download_url\":\"https://example.org/logo.png\"},{\"type\":\"file\",\"name\":\"run.exe\",\"download_url\":\"https://example.org/run.exe\"}]";
        assertEquals(Arrays.asList("https://example.org/screen.JPG"), PickerPictures.githubImages(contents, false));
        String releases = "[{\"assets\":[{\"name\":\"gameplay.png\",\"browser_download_url\":\"https://example.org/gameplay.png\"},{\"name\":\"game.zip\",\"browser_download_url\":\"https://example.org/game.zip\"}]}]";
        assertEquals(Arrays.asList("https://example.org/gameplay.png"), PickerPictures.githubImages(releases, true));
        assertTrue(PickerPictures.githubImages("{\"message\":\"Not Found\"}", false).isEmpty());
    }

    @Test public void rankVersionTriggersOncePerAppOrPolicyVersion() throws Exception {
        File dir = temp.newFolder();
        String version = PickerPictures.rankVersion("123:2.0");
        assertTrue(PickerPictures.needsRanking(dir, version));
        Files.write(new File(dir, "art.rgba").toPath(), new byte[] { 1 });
        Files.write(new File(dir, "rank-version").toPath(), version.getBytes("UTF-8"));
        assertFalse(PickerPictures.needsRanking(dir, version));
        assertTrue(PickerPictures.needsRanking(dir, PickerPictures.rankVersion("124:2.1")));
        Files.write(new File(dir, "rank-version").toPath(), "1:123:2.0".getBytes("UTF-8"));
        assertTrue(PickerPictures.needsRanking(dir, version));
    }

    @Test public void sharedBaseSplashIsNeverReadForOtherGames() throws Exception {
        File top = temp.newFolder();
        Map<String, byte[]> base = new LinkedHashMap<>();
        base.put("art/splash/splash0.rix", GameArtTest.rix(8, 6, 1));
        GameArtTest.dat2(new File(top, "master.dat"), base, true);
        assertNotNull(GameArt.pick(GameArt.sourceFor(top, "fallout2")));
        for (String id : GameRoots.IDS) {
            if (!"fallout2".equals(id)) assertNull(id, GameArt.pick(GameArt.sourceFor(top, id)));
        }
        File own = new File(top, "games/resurrection");
        assertTrue(own.mkdirs());
        Files.createSymbolicLink(new File(own, "master.dat").toPath(), new File(top, "master.dat").toPath());
        assertNull(GameArt.pick(GameArt.sourceFor(top, "resurrection")));
        File sharedData = new File(top, "data/art/splash");
        assertTrue(sharedData.mkdirs());
        Files.write(new File(sharedData, "splash0.rix").toPath(), GameArtTest.rix(8, 6, 1));
        Files.createSymbolicLink(new File(own, "data").toPath(), new File(top, "data").toPath());
        assertNull(GameArt.pick(GameArt.sourceFor(top, "resurrection")));
    }
}
