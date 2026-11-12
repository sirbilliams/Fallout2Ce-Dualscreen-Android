package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

// The HD keys, written without the game running: [hd] enabled=0|1 and the
// lighting quality ([hd_light] quality=low|medium|high; "off" is
// mode=original, the original light). No Android classes.
//
// The art pack is installed once (hd/<N>x) and hd_art_dir in fallout2.cfg
// stays global. Per mod profile, the switch and the quality live in
// profiles/<name>/hd.ini ([hd] enabled, [hd_light] mode/quality), which the
// engine lays over fallout2.cfg while that profile is active. Vanilla (or no
// active profile) uses fallout2.cfg itself. A profile whose hd.ini lacks a key
// falls back to fallout2.cfg for it.
final class HdSettings {
    static final String HD = "hd";
    static final String LIGHT = "hd_light";
    static final String PROFILE_FILE = "hd.ini";
    static final String[] QUALITIES = { "off", "low", "medium", "high" };

    private HdSettings() { }

    // The active mod profile, "" for Vanilla / none / a name without a
    // profiles/<name> folder (same rules as the engine).
    static String activeProfile(File game) {
        File file = new File(game, "profiles/active.txt");
        if (!file.isFile() || file.length() > 4096) return "";
        String name;
        try {
            String text = new String(java.nio.file.Files.readAllBytes(file.toPath()), java.nio.charset.StandardCharsets.UTF_8);
            int end = text.indexOf('\n');
            name = (end < 0 ? text : text.substring(0, end)).trim();
        } catch (IOException e) {
            return "";
        }
        if (name.isEmpty() || name.equals("Vanilla") || name.equals(".") || name.equals("..")
            || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) return "";
        return new File(game, "profiles/" + name).isDirectory() ? name : "";
    }

    // The profile's override file, null for Vanilla.
    static File profileFile(File game) {
        String name = activeProfile(game);
        return name.isEmpty() ? null : new File(game, "profiles/" + name + "/" + PROFILE_FILE);
    }

    // [section] key from the active profile's hd.ini, else fallout2.cfg.
    private static String get(File game, String section, String key, String fallback) {
        File profile = profileFile(game);
        if (profile != null) {
            String own = GuideSettings.getStringIn(profile, section, key, null);
            if (own != null) return own;
        }
        return GuideSettings.getString(game, section, key, fallback);
    }

    private static void set(File game, String section, String key, String value) throws IOException {
        File profile = profileFile(game);
        if (profile != null) GuideSettings.setStringIn(profile, section, key, value);
        else GuideSettings.setString(game, section, key, value);
    }

    static boolean isEnabled(File game) {
        try {
            return Integer.parseInt(get(game, HD, "enabled", "0").trim()) != 0;
        } catch (NumberFormatException e) {
            return false;
        }
    }

    // For the active profile only (fallout2.cfg for Vanilla). hd_art_dir stays
    // global: it points at the one shared pack.
    static void setEnabled(File game, boolean on) throws IOException {
        set(game, HD, "enabled", on ? "1" : "0");
        int tier = HdPackInstall.installedTier(game);
        if (on && tier != 0) GuideSettings.setString(game, HD, "hd_art_dir", HdPackInstall.DIR + "/" + tier + "x");
    }

    // One of QUALITIES; "high" when nothing is set (the engine's default).
    static String quality(File game) {
        if ("original".equalsIgnoreCase(get(game, LIGHT, "mode", "new"))) return "off";
        String q = get(game, LIGHT, "quality", "high").toLowerCase(Locale.ROOT);
        for (String known : QUALITIES) if (known.equals(q) && !q.equals("off")) return q;
        return "high";
    }

    static void setQuality(File game, String quality) throws IOException {
        if (quality.equals("off")) {
            set(game, LIGHT, "mode", "original");
            return;
        }
        boolean known = false;
        for (String q : QUALITIES) known |= q.equals(quality);
        if (!known) throw new IOException("bad quality " + quality);
        set(game, LIGHT, "mode", "new");
        set(game, LIGHT, "quality", quality);
    }
}
