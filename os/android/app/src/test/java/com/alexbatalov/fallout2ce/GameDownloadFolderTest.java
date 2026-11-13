package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.util.zip.*;
import static org.junit.Assert.*;

public class GameDownloadFolderTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    private static void zip(File f) throws IOException {
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(f))) {
            out.putNextEntry(new ZipEntry("data/a.txt"));
            out.write(1);
            out.closeEntry();
        }
    }

    @Test public void destinationAndUniquenessUseTheSameFolder() throws Exception {
        File folder = temp.newFolder("games", "sonora", "mod-downloads");
        File top = temp.newFolder("mod-downloads-top");
        assertTrue(new File(top, "x.zip").createNewFile()); // other folder must not matter
        assertEquals("x.zip", DownloadIndex.uniqueName(folder, "x.zip"));
        assertTrue(new File(folder, "x.zip").createNewFile());
        assertEquals("x-2.zip", DownloadIndex.uniqueName(folder, "x.zip"));
        assertEquals(folder, DownloadIndex.destination(folder, "x-2.zip").getParentFile());
    }

    @Test public void gameDownloadsIgnoreTheActiveGame() throws Exception {
        File top = temp.newFolder("top");
        assertEquals(new File(top, "mod-downloads"), GameInstall.downloadsFolder(top));
        File profiles = new File(top, "profiles");
        assertTrue(profiles.mkdirs());
        try (FileWriter w = new FileWriter(new File(profiles, "last-game.txt"))) { w.write("sonora\n"); }
        assertEquals("sonora", GameRoots.activeGameId(top));
        assertEquals(new File(top, "mod-downloads"), GameInstall.downloadsFolder(top));
        assertNotEquals(GameInstall.downloadsFolder(top), new File(GameRoots.gameRoot(top, "sonora"), "mod-downloads"));
    }

    @Test public void repairReusesExistingArchive() throws Exception {
        File folder = temp.newFolder("mod-downloads");
        File a = new File(folder, "game-sonora-Fallout_Sonora_1.16.4E_sfall.zip");
        zip(a);
        assertEquals(a, GameInstall.reusableArchive(folder, "sonora", null, -1));
        assertEquals(a, GameInstall.reusableArchive(folder, "sonora", "Fallout_Sonora_1.16.4E_sfall.zip", a.length()));
        assertNull(GameInstall.reusableArchive(folder, "sonora", "Fallout_Sonora_1.16.4E_sfall.zip", a.length() + 1));
        assertNull(GameInstall.reusableArchive(folder, "ettu", null, -1));
        File bad = new File(folder, "game-ettu-Fallout1in2.zip");
        try (FileWriter w = new FileWriter(bad)) { w.write("truncated"); }
        assertNull(GameInstall.reusableArchive(folder, "ettu", null, -1));

        // Adopting marks it a pending download of that game; no duplicate is created.
        DownloadIndex.update(folder, items -> items.add(new DownloadIndex.Item(5, a.getName(), "t", "game:sonora", DownloadIndex.DONE, true)));
        GameInstall.adoptArchive(folder, "sonora", a);
        DownloadIndex.Item item = DownloadIndex.find(DownloadIndex.read(folder), a.getName());
        assertFalse(item.installed);
        assertEquals("game:sonora", item.source);
        assertEquals(1, DownloadIndex.read(folder).size());
        assertEquals(2, folder.list((d, n) -> n.endsWith(".zip")).length);
    }
}
