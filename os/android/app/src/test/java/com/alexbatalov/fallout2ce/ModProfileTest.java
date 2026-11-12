package com.alexbatalov.fallout2ce;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Collections;

import static org.junit.Assert.*;

public class ModProfileTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    private static String read(File root, String path) throws Exception {
        return new String(Files.readAllBytes(new File(root, path).toPath()), StandardCharsets.UTF_8);
    }

    private static void write(File root, String path, String text) throws Exception {
        File f = new File(root, path);
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    @Test public void pickNameFree() {
        assertEquals("RPU", ModProfile.pickName(Collections.emptyList(), "RPU"));
    }

    @Test public void pickNameTaken() {
        assertEquals("RPU 2", ModProfile.pickName(Arrays.asList("RPU"), "RPU"));
        assertEquals("RPU 3", ModProfile.pickName(Arrays.asList("rpu", "RPU 2"), "RPU"));
    }

    @Test public void vanillaDetection() throws Exception {
        File root = temporary.newFolder();
        assertTrue(ModProfile.isVanilla(root));
        write(root, "profiles/active.txt", "Vanilla\n");
        assertTrue(ModProfile.isVanilla(root));
        write(root, "profiles/active.txt", "Mine\n");
        assertFalse(ModProfile.isVanilla(root));
    }

    @Test public void createsProfileLikeNativeCode() throws Exception {
        File root = temporary.newFolder();
        write(root, "ddraw.ini", "a=1");
        write(root, "mods/x/y.ini", "b=2");
        write(root, "mods/z.dat", "no");
        write(root, "profiles/Other/modlist.txt", "+a\n");
        ModProfile.Created c = ModProfile.createAndActivate(root, "RPU");
        assertEquals("RPU", c.name);
        assertNull(c.previousActive);
        assertEquals("RPU\n", read(root, "profiles/active.txt"));
        assertEquals("Vanilla\nOther\nRPU\n", read(root, "profiles/profiles.txt"));
        assertEquals("", read(root, "profiles/RPU/modlist.txt"));
        assertTrue(new File(root, "profiles/RPU/SAVEGAME").isDirectory());
        assertEquals("a=1", read(root, "profiles/RPU/ddraw.ini"));
        assertEquals("b=2", read(root, "profiles/RPU/mods/x/y.ini"));
        assertFalse(new File(root, "profiles/RPU/mods/z.dat").exists());
        // second one gets a new name and Other stays untouched
        ModProfile.Created d = ModProfile.createAndActivate(root, "RPU");
        assertEquals("RPU 2", d.name);
        assertEquals("RPU\n", d.previousActive);
        assertEquals("+a\n", read(root, "profiles/Other/modlist.txt"));
    }

    @Test public void rollbackRestoresPreviousState() throws Exception {
        File root = temporary.newFolder();
        write(root, "profiles/active.txt", "Vanilla\n");
        ModProfile.Created c = ModProfile.createAndActivate(root, "RPU");
        ModProfile.rollback(root, c);
        assertEquals("Vanilla\n", read(root, "profiles/active.txt"));
        assertFalse(new File(root, "profiles/RPU").exists());
        assertEquals("Vanilla\n", read(root, "profiles/profiles.txt"));
    }

    @Test public void rollbackWithoutPreviousActive() throws Exception {
        File root = temporary.newFolder();
        ModProfile.Created c = ModProfile.createAndActivate(root, "RPU");
        ModProfile.rollback(root, c);
        assertFalse(new File(root, "profiles/active.txt").exists());
        assertFalse(new File(root, "profiles/RPU").exists());
    }
}
