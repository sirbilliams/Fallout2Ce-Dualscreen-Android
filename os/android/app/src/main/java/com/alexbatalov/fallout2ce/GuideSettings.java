package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

// What the setup guide (SetupGuideActivity) reads and writes without the
// game running: the `[touch]` keys of the game's fallout2.cfg (the same ones
// the game's settings change: dashboard_layout, bottom_theme) and the mark
// that the guide was shown. No Android classes, so it is testable on a
// desktop JVM.
final class GuideSettings {
    static final String CONFIG = "fallout2.cfg";
    // Present once the guide was finished or skipped (it is shown once).
    static final String DONE_MARK = ".setup_guide_done";
    static final String SECTION = "touch";

    private GuideSettings() {
    }

    static boolean isDone(File game) {
        return game != null && new File(game, DONE_MARK).exists();
    }

    static void markDone(File game) {
        if (game == null) {
            return;
        }
        try {
            if (!game.isDirectory() && !game.mkdirs()) {
                return;
            }
            //noinspection ResultOfMethodCallIgnored
            new File(game, DONE_MARK).createNewFile();
        } catch (IOException ignored) {
        }
    }

    // The Setup guide setting: shown again.
    static void clearDone(File game) {
        if (game != null) {
            //noinspection ResultOfMethodCallIgnored
            new File(game, DONE_MARK).delete();
        }
    }

    // `[touch] key` as an int, [fallback] when there is none.
    static int getInt(File game, String key, int fallback) {
        List<String> lines = read(new File(game, CONFIG));
        boolean inside = false;
        for (String line : lines) {
            String trimmed = line.trim();
            if (trimmed.startsWith("[")) {
                inside = trimmed.equalsIgnoreCase("[" + SECTION + "]");
            } else if (inside && trimmed.toLowerCase(Locale.ROOT).startsWith(key + "=")) {
                try {
                    return Integer.parseInt(trimmed.substring(key.length() + 1).trim());
                } catch (NumberFormatException e) {
                    return fallback;
                }
            }
        }
        return fallback;
    }

    // Sets `[touch] key=value`, keeping every other line as it is (the
    // section and the key are added when missing).
    static void setInt(File game, String key, int value) throws IOException {
        File config = new File(game, CONFIG);
        List<String> lines = read(config);
        String entry = key + "=" + value;

        int sectionAt = -1;
        int sectionEnd = lines.size();
        for (int index = 0; index < lines.size(); index++) {
            String trimmed = lines.get(index).trim();
            if (trimmed.startsWith("[")) {
                if (sectionAt >= 0) {
                    sectionEnd = index;
                    break;
                }
                if (trimmed.equalsIgnoreCase("[" + SECTION + "]")) {
                    sectionAt = index;
                }
            }
        }

        if (sectionAt < 0) {
            if (!lines.isEmpty() && !lines.get(lines.size() - 1).trim().isEmpty()) {
                lines.add("");
            }
            lines.add("[" + SECTION + "]");
            lines.add(entry);
        } else {
            boolean replaced = false;
            for (int index = sectionAt + 1; index < sectionEnd; index++) {
                if (lines.get(index).trim().toLowerCase(Locale.ROOT).startsWith(key + "=")) {
                    lines.set(index, entry);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                int at = sectionEnd;
                // Before the blank lines that end the section.
                while (at > sectionAt + 1 && lines.get(at - 1).trim().isEmpty()) {
                    at--;
                }
                lines.add(at, entry);
            }
        }

        StringBuilder text = new StringBuilder();
        for (String line : lines) {
            text.append(line).append('\n');
        }
        Files.write(config.toPath(), text.toString().getBytes(StandardCharsets.ISO_8859_1));
    }

    private static List<String> read(File file) {
        List<String> lines = new ArrayList<>();
        if (!file.isFile()) {
            return lines;
        }
        try {
            String text = new String(Files.readAllBytes(file.toPath()), StandardCharsets.ISO_8859_1);
            for (String line : text.split("\r?\n", -1)) {
                lines.add(line);
            }
            // The text's last newline isn't a line.
            if (!lines.isEmpty() && lines.get(lines.size() - 1).isEmpty()) {
                lines.remove(lines.size() - 1);
            }
        } catch (IOException ignored) {
        }
        return lines;
    }
}
