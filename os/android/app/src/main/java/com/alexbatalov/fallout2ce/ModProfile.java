package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

// Creates a mod profile on disk the way the game's mod_profiles.cc does:
// profiles/<name>/{SAVEGAME/, modlist.txt, ddraw.ini, mods/**.ini},
// profiles/profiles.txt (Vanilla first, then the folders sorted) and
// profiles/active.txt. Used to install RPU without touching Vanilla.
final class ModProfile {
    static final String VANILLA = "Vanilla";

    private ModProfile() { }

    static final class Created {
        final String name;
        final String previousActive; // active.txt text before, null if none

        Created(String name, String previousActive) {
            this.name = name;
            this.previousActive = previousActive;
        }
    }

    static boolean isVanilla(File gameDir) throws IOException {
        String active = read(new File(gameDir, "profiles/active.txt")).trim();
        return active.isEmpty() || active.equals(VANILLA);
    }

    // [base], then "[base] 2", "[base] 3"... the first not in [existing]
    // (compared ignoring case; "Vanilla" and the files in profiles/ are taken).
    static String pickName(Collection<String> existing, String base) {
        List<String> taken = new ArrayList<>();
        for (String name : existing) taken.add(name.toLowerCase(Locale.ROOT));
        taken.add(VANILLA.toLowerCase(Locale.ROOT));
        taken.add("active.txt");
        taken.add("profiles.txt");
        String name = base;
        for (int n = 2; taken.contains(name.toLowerCase(Locale.ROOT)); n++) name = base + " " + n;
        return name;
    }

    // Creates an empty profile (settings copied from the game folder, as for
    // a new profile made from Vanilla) and makes it active.
    static Created createAndActivate(File gameDir, String base) throws IOException {
        File profiles = new File(gameDir, "profiles");
        String[] listed = profiles.list();
        List<String> existing = listed == null ? new ArrayList<>() : new ArrayList<>(java.util.Arrays.asList(listed));
        String name = pickName(existing, base);
        File dest = new File(profiles, name);
        if (!dest.mkdirs()) throw new IOException("can't create profile " + name);
        String previous = null;
        File activeFile = new File(profiles, "active.txt");
        if (activeFile.exists()) previous = read(activeFile);
        try {
            if (!new File(dest, "SAVEGAME").mkdir()) throw new IOException("can't create profile saves");
            write(new File(dest, "modlist.txt"), "");
            copySettings(gameDir, dest);
            writeNames(profiles);
            write(activeFile, name + "\n");
        } catch (IOException | RuntimeException e) {
            rollback(gameDir, new Created(name, previous));
            throw e;
        }
        return new Created(name, previous);
    }

    // Undoes createAndActivate (e.g. when the install failed).
    static void rollback(File gameDir, Created created) {
        File profiles = new File(gameDir, "profiles");
        try {
            File activeFile = new File(profiles, "active.txt");
            if (created.previousActive == null) Files.deleteIfExists(activeFile.toPath());
            else write(activeFile, created.previousActive);
        } catch (IOException ignored) {
            // best effort
        }
        GameFiles.deleteRecursively(new File(profiles, created.name));
        try {
            if (new File(profiles, "profiles.txt").exists()) writeNames(profiles);
        } catch (IOException ignored) {
            // best effort
        }
    }

    private static void writeNames(File profiles) throws IOException {
        List<String> names = new ArrayList<>();
        File[] dirs = profiles.listFiles(f -> f.isDirectory() && !f.getName().startsWith(".")
            && !f.getName().equals(VANILLA));
        if (dirs != null) for (File d : dirs) names.add(d.getName());
        Collections.sort(names);
        StringBuilder text = new StringBuilder(VANILLA).append('\n');
        for (String n : names) text.append(n).append('\n');
        write(new File(profiles, "profiles.txt"), text.toString());
    }

    private static void copySettings(File source, File dest) throws IOException {
        File ini = new File(source, "ddraw.ini");
        if (ini.isFile()) Files.copy(ini.toPath(), new File(dest, "ddraw.ini").toPath(), StandardCopyOption.REPLACE_EXISTING);
        copyInis(new File(source, "mods"), new File(dest, "mods"));
    }

    private static void copyInis(File from, File to) throws IOException {
        File[] children = from.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (child.isDirectory()) {
                copyInis(child, new File(to, child.getName()));
            } else if (child.isFile() && child.getName().endsWith(".ini")) {
                if (!to.isDirectory() && !to.mkdirs()) throw new IOException("can't create " + to);
                Files.copy(child.toPath(), new File(to, child.getName()).toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
        }
    }

    private static String read(File file) throws IOException {
        if (!file.exists()) return "";
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }

    private static void write(File file, String text) throws IOException {
        File parent = file.getParentFile();
        if (parent != null) parent.mkdirs();
        File temp = new File(file.getPath() + ".tmp");
        Files.write(temp.toPath(), text.getBytes(StandardCharsets.UTF_8));
        Files.move(temp.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING);
    }
}
