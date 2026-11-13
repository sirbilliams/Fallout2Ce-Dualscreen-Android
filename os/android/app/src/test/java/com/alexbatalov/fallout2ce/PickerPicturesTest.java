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
}
