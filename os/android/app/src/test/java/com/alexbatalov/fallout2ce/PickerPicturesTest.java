package com.alexbatalov.fallout2ce;

import org.junit.Test;
import java.io.*;
import java.nio.file.Files;
import static org.junit.Assert.*;

public class PickerPicturesTest {
    private String fixture(String name) throws IOException {
        try (InputStream in = getClass().getResourceAsStream("/picker/" + name)) {
            assertNotNull(in);
            return PickerPictures.utf8(PickerPictures.readLimited(in, 16384));
        }
    }
    @Test public void officialSourceSelection() throws Exception {
        assertEquals("https://api.github.com/repos/rotators/Fo1in2/readme", PickerPictures.readmeApi("https://github.com/rotators/Fo1in2/releases"));
        assertNull(PickerPictures.readmeApi("https://github.com.evil.invalid/rotators/Fo1in2"));
        String raw = PickerPictures.readmeUrl(fixture("readme.json"));
        assertEquals("https://raw.githubusercontent.com/rotators/Fo1in2/master/README.md", raw);
        assertEquals("https://raw.githubusercontent.com/rotators/Fo1in2/master/images/title.png", PickerPictures.imageUrl(raw, fixture("readme.md")));
        assertEquals("https://olympus2207.com/images/title.jpg?size=small&lang=en", PickerPictures.imageUrl("https://olympus2207.com/", fixture("project.html")));
        assertNull(PickerPictures.imageUrl("https://github.com/rotators/Fo1in2", "<meta property='og:image' content='https://opengraph.githubassets.com/hash/rotators/Fo1in2'>"));
        assertTrue(PickerPictures.isSocialCard("https://opengraph.githubassets.com/hash/rotators/Fo1in2"));
        assertFalse(PickerPictures.isSocialCard("https://raw.githubusercontent.com/rotators/Fo1in2/master/images/title.png"));
        assertNull(PickerPictures.imageUrl("https://resurrection.cz/", "<html>No image</html>"));
        assertNull(PickerPictures.readmeUrl("{\"download_url\":null}"));
        assertNull(PickerPictures.safeUrl("https://example.com", "http://example.com/image"));
        assertNull(PickerPictures.safeUrl("https://example.com", "https://127.0.0.1/image"));
        assertNull(PickerPictures.safeUrl("https://example.com", "file:///etc/passwd"));
    }
    @Test public void pathsAndOfflineFallback() throws Exception {
        File top = Files.createTempDirectory("picker-cache-test").toFile();
        File preview = new File(PickerPictures.directory(top, "sonora", false), "art.rgba");
        File full = new File(PickerPictures.directory(top, "sonora", true), "art.rgba");
        assertEquals(new File(top, "picker-preview/sonora/art.rgba"), preview);
        assertEquals(new File(top, "games/sonora/picker/art.rgba"), full);
        assertNull(PickerPictures.cached(top, "sonora")); // Styled title, no network required.
        assertTrue(preview.getParentFile().mkdirs());
        Files.write(preview.toPath(), new byte[]{1});
        assertEquals(preview, PickerPictures.cached(top, "sonora"));
        assertTrue(full.getParentFile().mkdirs());
        Files.write(full.toPath(), new byte[]{2});
        assertEquals(full, PickerPictures.cached(top, "sonora"));
        assertFalse(PickerPictures.mayFetch(false, false, false));
        assertFalse(PickerPictures.mayFetch(true, false, true));
        assertFalse(PickerPictures.mayFetch(true, true, true));
        assertTrue(PickerPictures.mayFetch(true, true, false));
        // Only this test's temporary files.
        try (java.util.stream.Stream<java.nio.file.Path> paths = Files.walk(top.toPath())) {
            paths.sorted(java.util.Comparator.reverseOrder()).forEach(p -> { try { Files.delete(p); } catch (IOException e) { throw new UncheckedIOException(e); } });
        }
    }
    @Test(expected=IllegalArgumentException.class) public void traversalRejected() {
        PickerPictures.directory(new File("/top"), "../sonora", false);
    }
    @Test public void streamCapIncludesUnknownLength() throws Exception {
        byte[] bytes = new byte[PickerPictures.PREVIEW_CAP];
        assertEquals(bytes.length, PickerPictures.readLimited(new ByteArrayInputStream(bytes), bytes.length).length);
        try {
            PickerPictures.readLimited(new ByteArrayInputStream(new byte[bytes.length + 1]), bytes.length);
            fail("oversize stream accepted");
        } catch (IOException expected) { }
    }
    @Test public void installedSocialCardArtIsPurgedForRegeneration() throws Exception {
        File top = Files.createTempDirectory("picker-stale-test").toFile();
        File dir = PickerPictures.directory(top, "sonora", true);
        assertTrue(dir.mkdirs());
        Files.write(new File(dir, "art.rgba").toPath(), new byte[]{1});
        // Art without credit (or with a real image) is kept.
        assertFalse(PickerPictures.purgeSocialCard(dir));
        Files.write(new File(dir, "credit.txt").toPath(), "by X - https://github.com/a/b\nImage: https://raw.githubusercontent.com/a/b/master/t.png\n".getBytes("UTF-8"));
        assertFalse(PickerPictures.purgeSocialCard(dir));
        assertTrue(new File(dir, "art.rgba").isFile());
        // A GitHub social card is removed so it is rebuilt (plain panel meanwhile).
        Files.write(new File(dir, "credit.txt").toPath(), "by X - https://github.com/a/b\nImage: https://opengraph.githubassets.com/abc/a/b\n".getBytes("UTF-8"));
        assertTrue(PickerPictures.purgeSocialCard(dir));
        assertFalse(new File(dir, "art.rgba").exists());
        assertFalse(new File(dir, "credit.txt").exists());
        assertNull(PickerPictures.cached(top, "sonora"));
        assertFalse(PickerPictures.purgeSocialCard(dir)); // nothing left
    }

    @Test public void rankingSkipsBadgesLogosAndSocialCardsAndPrefersLargeLandscape() {
        String md = "[![Build](https://img.shields.io/github/actions/workflow/status/x/y/ci.yml/badge.svg)](https://ci)\n"
            + "![logo](docs/logo.png)\n![icon](https://example.org/icon-64.png)\n"
            + "<img src=\"https://opengraph.githubassets.com/abc/x/y\">\n"
            + "![shot](https://example.org/shot.jpg)\n<img src='https://example.org/wide.png' width=800>\n";
        java.util.List<String> urls = PickerPictures.imageUrls("https://raw.githubusercontent.com/x/y/main/README.md", md, 6);
        assertEquals(java.util.Arrays.asList("https://example.org/shot.jpg", "https://example.org/wide.png"), urls);
        assertTrue(PickerPictures.unwantedImage("https://img.shields.io/badge/a-b.svg"));
        assertTrue(PickerPictures.unwantedImage("https://example.org/Logo.png"));
        assertFalse(PickerPictures.unwantedImage("https://example.org/screenshot.png"));
        assertEquals(-1, PickerPictures.imageScore(320, 200));   // too narrow
        assertEquals(-1, PickerPictures.imageScore(1000, 1000)); // square
        assertEquals(-1, PickerPictures.imageScore(1600, 400));  // too wide
        assertEquals(-1, PickerPictures.imageScore(640, 480));
        assertTrue(PickerPictures.imageScore(1280, 720) > PickerPictures.imageScore(640, 480));
    }
}
