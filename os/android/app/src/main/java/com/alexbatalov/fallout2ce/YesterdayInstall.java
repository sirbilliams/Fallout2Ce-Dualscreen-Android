package com.alexbatalov.fallout2ce;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

// GitLab's main archive moves: recorded size/hash are informational, not a pin.
// ZipFile + per-entry size/CRC checks and the expected layout validate downloads.
final class YesterdayInstall {
    static final String URL = "https://gitlab.com/falloutfans/fallouty/-/archive/main/fallouty-main.zip";
    static final String PREFIX = "fallouty-main/";
    static final long ARCHIVE_BYTES = 4481987989L;
    static final String ARCHIVE_SHA256 = "b4dd0742ba85b75cb5fde024b2c6045558046cc3f9a8dfacc135170c7cef5f87";
    // Report: 9.57 GB unpacked; round up even if interpreted as GiB.
    static final long UNPACKED_BYTES = 10300000000L;
    static final String LFS_HEADER = "version https://git-lfs.github.com/spec/v1";

    static long preflight() throws IOException {
        return GameInstall.needed(ARCHIVE_BYTES, UNPACKED_BYTES);
    }

    static void validate(Set<String> paths) throws IOException {
        if (!paths.contains("fallouty.cfg") || !paths.contains("ddraw.ini")
            || !paths.contains("patch002.dat")
            || paths.stream().noneMatch(p -> p.startsWith("patch001.dat/"))
            || paths.stream().noneMatch(p -> p.startsWith("data/")))
            throw new IOException("Not a Fallout: Yesterday main archive");
    }

    // Never publish pointer text as movie/audio data. Skip explicitly and keep a
    // persistent log in the game folder, also visible in Android's process log.
    static boolean skipPointer(File file, String path, StringBuilder log) throws IOException {
        if (file.length() > 1024) return false;
        String text = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        if (!text.startsWith(LFS_HEADER)) return false;
        String line = "Skipped Git LFS pointer: " + path + " (real media not included in archive)\n";
        log.append(line);
        System.err.print("Yesterday installer: " + line);
        if (!file.delete()) throw new IOException("Cannot discard staged LFS pointer");
        return true;
    }

    static void prepare(File staged, StringBuilder log) throws IOException {
        // CE loads mods via mods_order.txt, not the Windows DLL's sfall.dat hook.
        File sfall = new File(staged, "mods/sfall.dat");
        if (sfall.isFile()) {
            File order = new File(staged, "mods/mods_order.txt");
            String existing = order.isFile() ? new String(Files.readAllBytes(order.toPath()), StandardCharsets.ISO_8859_1) : "";
            if (Arrays.stream(existing.split("\\r?\\n")).noneMatch(l -> l.trim().equalsIgnoreCase("sfall.dat")))
                Files.write(order.toPath(), (existing + "\n" + "sfall.dat\n").getBytes(StandardCharsets.ISO_8859_1));
        }
        File original = new File(staged, "fallouty.cfg");
        File cfg = new File(staged, "fallout2.cfg");
        // Use one canonical config for native settings and Android helpers.
        String text = new String(Files.readAllBytes(original.toPath()), StandardCharsets.ISO_8859_1);
        Files.write(cfg.toPath(), text.replace('\\', '/').getBytes(StandardCharsets.ISO_8859_1));
        GameInstall.writeConfig(staged);
        Files.copy(cfg.toPath(), original.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        File ddraw = new File(staged, "ddraw.ini");
        List<String> lines = Files.readAllLines(ddraw.toPath(), StandardCharsets.ISO_8859_1);
        StringBuilder out = new StringBuilder();
        boolean misc = false;
        for (String line : lines) {
            String trimmed = line.trim().toLowerCase(Locale.ROOT);
            if (trimmed.startsWith("[")) misc = trimmed.equals("[misc]");
            if (misc && trimmed.matches("configfile\\s*=.*")) continue;
            out.append(line).append('\n');
            if (trimmed.equals("[misc]")) out.append("ConfigFile=fallout2.cfg\n");
        }
        if (lines.stream().noneMatch(l -> l.trim().equalsIgnoreCase("[misc]")))
            out.append("\n[Misc]\nConfigFile=fallout2.cfg\n");
        Files.write(ddraw.toPath(), out.toString().getBytes(StandardCharsets.ISO_8859_1));
        if (log.length() > 0)
            Files.write(new File(staged, "yesterday-install.log").toPath(), log.toString().getBytes(StandardCharsets.UTF_8));
    }
}
