package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

// The HD keys of the game's fallout2.cfg, written without the game running:
// [hd] enabled=0|1 (and hd_art_dir to the installed tier) and the lighting
// quality ([hd_light] quality=low|medium|high; "off" is mode=original, the
// original light). No Android classes.
final class HdSettings {
    static final String HD = "hd";
    static final String LIGHT = "hd_light";
    static final String[] QUALITIES = { "off", "low", "medium", "high" };

    private HdSettings() { }

    static boolean isEnabled(File game) {
        return GuideSettings.getInt(game, HD, "enabled", 0) != 0;
    }

    static void setEnabled(File game, boolean on) throws IOException {
        GuideSettings.setString(game, HD, "enabled", on ? "1" : "0");
        int tier = HdPackInstall.installedTier(game);
        if (on && tier != 0) GuideSettings.setString(game, HD, "hd_art_dir", HdPackInstall.DIR + "/" + tier + "x");
    }

    // One of QUALITIES; "high" when nothing is set (the engine's default).
    static String quality(File game) {
        if ("original".equalsIgnoreCase(GuideSettings.getString(game, LIGHT, "mode", "new"))) return "off";
        String q = GuideSettings.getString(game, LIGHT, "quality", "high").toLowerCase(Locale.ROOT);
        for (String known : QUALITIES) if (known.equals(q) && !q.equals("off")) return q;
        return "high";
    }

    static void setQuality(File game, String quality) throws IOException {
        if (quality.equals("off")) {
            GuideSettings.setString(game, LIGHT, "mode", "original");
            return;
        }
        boolean known = false;
        for (String q : QUALITIES) known |= q.equals(quality);
        if (!known) throw new IOException("bad quality " + quality);
        GuideSettings.setString(game, LIGHT, "mode", "new");
        GuideSettings.setString(game, LIGHT, "quality", quality);
    }
}
