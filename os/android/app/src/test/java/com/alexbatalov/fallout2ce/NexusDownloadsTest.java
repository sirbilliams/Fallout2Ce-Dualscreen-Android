package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.util.Arrays;
import java.util.List;

import static org.junit.Assert.*;

public class NexusDownloadsTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    @Test public void fileNameFromDisposition() {
        assertEquals("Mod v1.zip", NexusDownloads.fileName("attachment; filename=\"Mod v1.zip\"", "https://x/y.zip"));
        assertEquals("a.7z", NexusDownloads.fileName("attachment; filename=a.7z; size=3", null));
        assertEquals("Café.zip", NexusDownloads.fileName(
            "attachment; filename=\"fallback.zip\"; filename*=UTF-8''Caf%C3%A9.zip", "https://x/z"));
    }

    @Test public void fileNameFromUrlAndSafe() {
        assertEquals("my mod.zip", NexusDownloads.fileName(null, "https://cdn.x/a/my%20mod.zip?md5=1&e=2"));
        assertEquals("mod.zip", NexusDownloads.fileName(null, null));
        assertEquals("evil.zip", NexusDownloads.fileName("attachment; filename=\"../../evil.zip\"", null));
        assertEquals("mod.zip", NexusDownloads.fileName("attachment; filename=\"..\"", null));
    }

    @Test public void archiveUrls() {
        assertTrue(NexusDownloads.isArchiveUrl("https://cdn.nexusmods.com/f/a.ZIP?x=1"));
        assertTrue(NexusDownloads.isArchiveUrl("https://a/b.7z"));
        assertTrue(NexusDownloads.isArchiveUrl("https://a/b.rar#x"));
        assertFalse(NexusDownloads.isArchiveUrl("https://www.nexusmods.com/fallout2/mods/1?file=a.zip"));
        assertFalse(NexusDownloads.isArchiveUrl(null));
    }

    @Test public void uniqueAndModName() {
        List<String> have = Arrays.asList("a.zip", "a-2.zip");
        assertEquals("a-3.zip", NexusDownloads.uniqueFile(have, "a.zip"));
        assertEquals("b.zip", NexusDownloads.uniqueFile(have, "b.zip"));
        assertEquals("Cool Mod", NexusDownloads.modName("Cool Mod at Fallout 2 Nexus - Mods and community", "x.zip"));
        assertEquals("x", NexusDownloads.modName("", "x.zip"));
    }

    @Test public void indexRoundTripAndPending() throws Exception {
        File dir = temporary.newFolder();
        assertTrue(NexusDownloads.pending(dir).isEmpty());
        new File(dir, "a.zip").createNewFile();
        new File(dir, "b.zip").createNewFile();
        NexusDownloads.add(dir, "a.zip", "Mod A");
        NexusDownloads.add(dir, "b.zip", "Mod B");
        NexusDownloads.add(dir, "gone.zip", "Missing");
        assertEquals(3, NexusDownloads.read(dir).size());
        List<NexusDownloads.Entry> pending = NexusDownloads.pending(dir);
        assertEquals(2, pending.size());
        assertEquals("Mod A", pending.get(0).title);
        NexusDownloads.remove(dir, "a.zip");
        pending = NexusDownloads.pending(dir);
        assertEquals(1, pending.size());
        assertEquals("b.zip", pending.get(0).file);
        // re-adding the same file replaces, not duplicates
        NexusDownloads.add(dir, "b.zip", "Mod B2");
        assertEquals("Mod B2", NexusDownloads.read(dir).get(NexusDownloads.read(dir).size() - 1).title);
    }
}
