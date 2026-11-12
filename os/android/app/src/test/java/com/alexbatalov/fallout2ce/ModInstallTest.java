package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;

import static org.junit.Assert.*;

public class ModInstallTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();
    private static final ModInstall.Progress PROGRESS = new ModInstall.Progress() {
        public void update(long done, long total, String path) { }
        public boolean isCancelled() { return false; }
    };

    // Real ZIP fixtures, exposed through the same Source interface as JNI.
    private GameFilesTest.MemorySource zip(String... paths) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream out = new ZipOutputStream(bytes)) {
            for (String path : paths) {
                out.putNextEntry(new ZipEntry(path));
                out.write("fixture".getBytes(StandardCharsets.UTF_8));
                out.closeEntry();
            }
        }
        GameFilesTest.MemorySource source = new GameFilesTest.MemorySource();
        try (ZipInputStream in = new ZipInputStream(new ByteArrayInputStream(bytes.toByteArray()))) {
            ZipEntry entry;
            while ((entry = in.getNextEntry()) != null) {
                ByteArrayOutputStream data = new ByteArrayOutputStream();
                byte[] buffer = new byte[1024];
                int read;
                while ((read = in.read(buffer)) != -1) data.write(buffer, 0, read);
                source.files.put(entry.getName(), data.toByteArray());
            }
        }
        return source;
    }

    private void write(File root, String path, String text) throws IOException {
        File file = new File(root, path);
        file.getParentFile().mkdirs();
        Files.write(file.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    private String read(File root, String path) throws IOException {
        return new String(Files.readAllBytes(new File(root, path).toPath()), StandardCharsets.UTF_8);
    }

    private File profile() throws IOException {
        File root = temporary.newFolder();
        write(root, "profiles/active.txt", "Тест run\n");
        write(root, "profiles/Тест run/modlist.txt", "-old");
        return root;
    }

    @Test public void fixtureRoots() throws Exception {
        assertEquals("", ModInstall.detectRoot(zip("data/text/a.msg").list()));
        assertEquals("MyMod/", ModInstall.detectRoot(zip("MyMod/data/text/a.msg", "readme.txt").list()));
        assertEquals("", ModInstall.detectRoot(zip("x.dat").list()));
        assertEquals("MyMod/", ModInstall.detectRoot(zip("MyMod/mods/x.dat").list()));
        assertEquals("", ModInstall.detectRoot(zip("data/art/x.frm", "tools/x.dat").list()));
        assertNull(ModInstall.detectRoot(zip("readme.txt").list()));
    }

    @Test public void emptyRefusedWithSpecificMessage() throws Exception {
        try {
            ModInstall.install(zip("readme.txt"), "empty.zip", profile(), PROGRESS);
            fail();
        } catch (ModInstall.Problem e) {
            assertEquals(R.string.mod_no_files, e.message);
        }
    }

    @Test public void idsMirrorNativeRules() {
        assertEquals("my-mod--2", ModInstall.uniqueId(Arrays.asList("my-mod-"), "My Mod!"));
        assertEquals("name-3", ModInstall.uniqueId(Arrays.asList("name", "name-2"), "NAME"));
        assertEquals("a.b_c-d", ModInstall.uniqueId(Collections.emptyList(), "A.B_C-D"));
        assertEquals("--", ModInstall.uniqueId(Collections.emptyList(), "é"));
        assertEquals("", ModInstall.uniqueId(Collections.emptyList(), ""));
        assertEquals("..", ModInstall.uniqueId(Collections.emptyList(), ".."));
    }

    @Test public void nestedInstallSkipsWindowsAndAppendsUniqueIds() throws Exception {
        File root = profile();
        GameFilesTest.MemorySource source = zip("MyMod/Data/text/a.msg", "MyMod/mods/X.DAT", "MyMod/setup.exe", "MyMod/ddraw.dll", "MyMod/other.DLL");
        ModInstall.Result result = ModInstall.install(source, "My Mod!.zip", root, PROGRESS);
        assertEquals("my-mod-", result.id);
        assertEquals(Arrays.asList("setup.exe", "ddraw.dll", "other.DLL"), result.skipped);
        assertEquals("fixture", read(root, "modstore/my-mod-/data/text/a.msg"));
        assertEquals("fixture", read(root, "modstore/my-mod-/x.dat"));
        assertFalse(new File(root, "modstore/my-mod-/setup.exe").exists());
        ModInstall.install(source, "My Mod!.zip", root, PROGRESS);
        assertEquals("-old\n+my-mod-\n+my-mod--2\n", read(root, "profiles/Тест run/modlist.txt"));
        assertFalse(new File(root, "modstore/.tmp-my-mod-").exists());
        assertFalse(new File(root, "data").exists());
    }

    @Test public void failedAndKilledInstallLeaveNoHalfMod() throws Exception {
        File root = profile();
        GameFilesTest.MemorySource source = zip("data/a.msg");
        source.chunk = 1;
        source.failOn = "data/a.msg";
        try {
            ModInstall.install(source, "broken.zip", root, PROGRESS);
            fail();
        } catch (IOException expected) { }
        assertFalse(new File(root, "modstore/broken").exists());
        assertFalse(new File(root, "modstore/.tmp-broken").exists());
        assertEquals("-old", read(root, "profiles/Тест run/modlist.txt"));
        write(root, "modstore/.tmp-killed/data/a.msg", "partial");
        write(root, "modstore/complete/data/a.msg", "keep");
        ModInstall.cleanup(root);
        assertFalse(new File(root, "modstore/.tmp-killed").exists());
        assertEquals("keep", read(root, "modstore/complete/data/a.msg"));
    }

    @Test public void vanillaOnlyAddsDatsAndPreservesExistingFiles() throws Exception {
        File root = temporary.newFolder();
        write(root, "mods/mods_order.txt", "old.dat");
        write(root, "mods/old.dat", "old");
        write(root, "mods/test.dat", "collision");
        write(root, "data/SAVEGAME/SLOT01/SAVE.DAT", "save");
        ModInstall.install(zip("Wrap/mods/a.dat", "Wrap/mods/b.dat"), "test.zip", root, PROGRESS);
        assertEquals("old.dat\ntest-2.dat\ntest-3.dat\n", read(root, "mods/mods_order.txt"));
        assertEquals("old", read(root, "mods/old.dat"));
        assertEquals("collision", read(root, "mods/test.dat"));
        assertEquals("fixture", read(root, "mods/test-2.dat"));
        assertEquals("save", read(root, "data/SAVEGAME/SLOT01/SAVE.DAT"));
    }

    @Test public void vanillaLooseModRequiresProfile() throws Exception {
        File root = temporary.newFolder();
        try {
            ModInstall.install(zip("data/a.msg"), "loose.zip", root, PROGRESS);
            fail();
        } catch (ModInstall.Problem e) {
            assertEquals(R.string.mod_vanilla_dat_only, e.message);
        }
        assertFalse(new File(root, "data/a.msg").exists());
    }

    @Test public void traversalCannotEscapeWrapper() throws Exception {
        try {
            ModInstall.install(zip("Wrapper/../data/a.msg"), "unsafe.zip", profile(), PROGRESS);
            fail();
        } catch (IOException expected) { }
    }

    @Test public void rejectsTwiceUnpackedSizeBeforeReading() throws Exception {
        File root = profile();
        GameImport.Source huge = new GameImport.Source() {
            List<GameImport.Entry> list() {
                return Collections.singletonList(new GameImport.Entry("x.dat", root.getUsableSpace(), null, false, false));
            }
            void read(List<GameImport.Entry> entries, GameImport.EntrySink sink) { fail("must refuse before unpacking"); }
        };
        try {
            ModInstall.install(huge, "huge.zip", root, PROGRESS);
            fail();
        } catch (ModInstall.Problem e) {
            assertEquals(R.string.mod_no_space, e.message);
            assertTrue(e.neededMb > 0);
        }
    }
    @Test public void rejectsIniOnlyAndAmbiguousRoots() throws Exception {
        assertNull(GameImport.findMod(zip("mods/settings.ini")));
        try {
            GameImport.findMod(zip("A/data/a.msg", "B/data/b.msg"));
            fail();
        } catch (GameImport.SeveralGamesException expected) {
            assertEquals(2, expected.roots.size());
        }
    }

    @Test public void detectsDatWhoseNameEndsWithCeAndUnicodeWrapper() throws Exception {
        assertEquals("", ModInstall.detectRoot(zip("nice.dat").list()));
        assertEquals("İMod/", ModInstall.detectRoot(zip("İMod/Data/text/a.msg").list()));
    }

    @Test public void duplicateNormalizedPathsRefused() throws Exception {
        try {
            ModInstall.install(zip("x.dat", "mods/X.DAT"), "duplicate.zip", profile(), PROGRESS);
            fail();
        } catch (IOException expected) { }
    }

    @Test public void cancelledInstallNeverPublishes() throws Exception {
        File root = profile();
        try {
            ModInstall.install(zip("x.dat"), "cancelled.zip", root, new ModInstall.Progress() {
                public void update(long done, long total, String path) { }
                public boolean isCancelled() { return true; }
            });
            fail();
        } catch (GameImport.CancelledException expected) { }
        assertFalse(new File(root, "modstore/cancelled").exists());
        assertEquals("-old", read(root, "profiles/Тест run/modlist.txt"));
    }

    @Test public void startupFinishesVanillaDatAndOrderJournal() throws Exception {
        File root = temporary.newFolder();
        write(root, "mods/.import-staging/new.dat", "dat");
        write(root, "mods/.import-staging/mods_order.txt", "old.dat\nnew.dat\n");
        write(root, "mods/.import-commit", "M\tnew.dat\tnew.dat\nM\tmods_order.txt\tmods_order.txt\n");
        ModInstall.cleanup(root);
        assertEquals("dat", read(root, "mods/new.dat"));
        assertEquals("old.dat\nnew.dat\n", read(root, "mods/mods_order.txt"));
        assertFalse(new File(root, "mods/.import-commit").exists());
    }

}
