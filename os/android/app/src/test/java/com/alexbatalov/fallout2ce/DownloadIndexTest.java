package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static org.junit.Assert.*;

public class DownloadIndexTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    private File file(File dir, String name, int size) throws Exception {
        File f = new File(dir, name);
        Files.write(f.toPath(), new byte[size]);
        return f;
    }

    @Test public void roundTrip() throws Exception {
        File dir = temporary.newFolder();
        assertTrue(DownloadIndex.read(dir).isEmpty());
        DownloadIndex.update(dir, items -> {
            items.add(new DownloadIndex.Item(7, "a.zip", "Mod\tA", "nexus", DownloadIndex.QUEUED, false));
            items.add(new DownloadIndex.Item(9, "b.zip", "Mod B", "rpu", DownloadIndex.DONE, true));
        });
        List<DownloadIndex.Item> back = DownloadIndex.read(dir);
        assertEquals(2, back.size());
        assertEquals(7, back.get(0).id);
        assertEquals("Mod A", back.get(0).title);
        assertEquals("rpu", back.get(1).source);
        assertTrue(back.get(1).installed);
        DownloadIndex.update(dir, items -> DownloadIndex.find(items, "a.zip").installed = true);
        assertTrue(DownloadIndex.find(DownloadIndex.read(dir), "a.zip").installed);
    }

    @Test public void migratesOldIndex() throws Exception {
        File dir = temporary.newFolder();
        Files.write(new File(dir, "downloads.tsv").toPath(),
            "a.zip\tCool Mod\nb.zip\tOther\n\nbroken\n../evil.zip\tX\n".getBytes(StandardCharsets.UTF_8));
        List<DownloadIndex.Item> items = DownloadIndex.read(dir);
        assertEquals(3, items.size());
        assertEquals(-1, items.get(0).id);
        assertEquals("a.zip", items.get(0).file);
        assertEquals("Cool Mod", items.get(0).title);
        assertEquals("nexus", items.get(0).source);
        assertEquals(DownloadIndex.DONE, items.get(0).status);
        assertFalse(items.get(0).installed);
        assertEquals("evil.zip", items.get(2).file);
        // migrated file is rewritten in the new format on the next update
        DownloadIndex.update(dir, list -> { });
        assertEquals(3, DownloadIndex.read(dir).size());
        assertTrue(new String(Files.readAllBytes(new File(dir, "downloads.tsv").toPath()), StandardCharsets.UTF_8)
            .startsWith("-1\ta.zip\tCool Mod\tnexus\tdone\t0"));
    }

    @Test public void statusMapping() {
        assertEquals(DownloadIndex.Kind.DOWNLOADED, DownloadIndex.map(new DownloadIndex.Live(8, 0, 10, 10), false).kind);
        assertEquals(DownloadIndex.Kind.INSTALLED, DownloadIndex.map(new DownloadIndex.Live(8, 0, 10, 10), true).kind);
        DownloadIndex.State running = DownloadIndex.map(new DownloadIndex.Live(2, 0, 25, 100), false);
        assertEquals(DownloadIndex.Kind.DOWNLOADING, running.kind);
        assertEquals(25, running.percent);
        assertEquals(0, DownloadIndex.map(new DownloadIndex.Live(1, 0, 0, -1), false).percent);
        assertEquals(-1, DownloadIndex.map(new DownloadIndex.Live(2, 0, 500, -1), false).percent);
        assertEquals(DownloadIndex.Kind.WAITING_WIFI, DownloadIndex.map(new DownloadIndex.Live(4, 3, 5, 10), false).kind);
        assertEquals(DownloadIndex.Kind.WAITING_NETWORK, DownloadIndex.map(new DownloadIndex.Live(4, 2, 5, 10), false).kind);
        assertEquals(DownloadIndex.Kind.WAITING_NETWORK, DownloadIndex.map(new DownloadIndex.Live(4, 1, 5, 10), false).kind);
        DownloadIndex.State failed = DownloadIndex.map(new DownloadIndex.Live(16, 1006, 0, 10), false);
        assertEquals(DownloadIndex.Kind.FAILED, failed.kind);
        assertEquals(1006, failed.reason);
        assertEquals(100, DownloadIndex.map(new DownloadIndex.Live(2, 0, 200, 100), false).percent);
    }

    @Test public void rowsListUnindexedFilesAndSkipGone() throws Exception {
        File dir = temporary.newFolder();
        file(dir, "done.zip", 2048);
        file(dir, "installed.zip", 1024);
        file(dir, "loose.zip", 4096);
        file(dir, "notes.txt", 10);
        file(dir, "running.zip", 300);
        file(dir, "old.7z", 5);
        List<DownloadIndex.Item> index = Arrays.asList(
            new DownloadIndex.Item(1, "done.zip", "Done", "nexus", DownloadIndex.DONE, false),
            new DownloadIndex.Item(2, "installed.zip", "Inst", "nexus", DownloadIndex.DONE, true),
            new DownloadIndex.Item(3, "running.zip", "Run", "rpu", DownloadIndex.QUEUED, false),
            new DownloadIndex.Item(4, "pending.zip", "Pending", "nexus", DownloadIndex.QUEUED, false),
            new DownloadIndex.Item(5, "gone.zip", "Gone", "nexus", DownloadIndex.DONE, false));
        Map<Long, DownloadIndex.Live> live = new HashMap<>();
        live.put(3L, new DownloadIndex.Live(2, 0, 300, 1000));
        live.put(4L, new DownloadIndex.Live(1, 0, 0, -1));
        List<DownloadIndex.Row> rows = DownloadIndex.rows(dir, index, live);
        // done, installed, running, pending(no file yet), loose, old.7z; not notes.txt, not gone.zip
        assertEquals(6, rows.size());
        assertTrue(rows.get(0).state.active());
        assertTrue(rows.get(1).state.active());
        boolean sawLoose = false;
        for (DownloadIndex.Row r : rows) {
            if (r.item.file.equals("loose.zip")) {
                sawLoose = true;
                assertFalse(r.indexed);
                assertEquals(DownloadIndex.Kind.DOWNLOADED, r.state.kind);
                assertEquals("loose", r.item.title);
            }
            if (r.item.file.equals("installed.zip")) assertEquals(DownloadIndex.Kind.INSTALLED, r.state.kind);
            assertNotEquals("notes.txt", r.item.file);
            assertNotEquals("gone.zip", r.item.file);
        }
        assertTrue(sawLoose);
        // 2048 + 1024 + 4096 + 300 + 0 (pending: no file, 0 bytes so far) + 5
        assertEquals(7473, DownloadIndex.totalSize(rows));
        assertEquals(3, DownloadIndex.readyCount(rows));  // done, loose, old.7z
    }

    @Test public void stateWithoutManager() {
        DownloadIndex.Item queued = new DownloadIndex.Item(8, "a.zip", "A", "nexus", DownloadIndex.QUEUED, false);
        assertEquals(DownloadIndex.Kind.FAILED, DownloadIndex.state(queued, null, true).kind);
        DownloadIndex.Item done = new DownloadIndex.Item(8, "a.zip", "A", "nexus", DownloadIndex.DONE, false);
        assertEquals(DownloadIndex.Kind.DOWNLOADED, DownloadIndex.state(done, null, true).kind);
        done.installed = true;
        assertEquals(DownloadIndex.Kind.INSTALLED, DownloadIndex.state(done, null, true).kind);
    }

    @Test public void sizes() {
        assertEquals("512 B", DownloadIndex.formatSize(512));
        assertEquals("12 KB", DownloadIndex.formatSize(12 * 1024));
        assertEquals("320 MB", DownloadIndex.formatSize(320L << 20));
        assertEquals("1.5 GB", DownloadIndex.formatSize(3L << 29));
    }
}
