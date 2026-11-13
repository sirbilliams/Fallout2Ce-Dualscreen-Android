package com.alexbatalov.fallout2ce;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

// Where each game's data lives. Mirrors src/game_catalog.cc: Fallout 2 keeps
// the app's external files dir (the "top root") exactly as before; every other
// game owns "<top>/games/<id>". The selection is "<top>/profiles/last-game.txt"
// (written by the native picker); an unknown or missing id means Fallout 2.
// Vanilla bases shared by expansions: Fallout 2 = the top root, Fallout 1 =
// "<top>/base/fallout1". Pure helpers take the top File so JVM tests can use them.
public final class GameRoots {
    public static final String FALLOUT2 = "fallout2";
    // Keep in sync with gameCatalog() in src/game_catalog.cc (GameRootsTest checks it).
    static final String[] IDS = {
        "fallout2", "ettu", "sonora", "resurrection", "nevada", "olympus2207", "megamod", "yesterday"
    };
    static final String SELECTION_FILE = "profiles/last-game.txt";

    private GameRoots() { }

    /** The app data root; the selection file and global files live here. May be null without storage. */
    public static File topRoot(Context context) {
        return context.getExternalFilesDir(null);
    }

    public static String activeGameId(Context context) {
        return activeGameId(topRoot(context));
    }

    public static File activeGameRoot(Context context) {
        File top = topRoot(context);
        return gameRoot(top, activeGameId(top));
    }

    public static File gameRoot(Context context, String id) {
        return gameRoot(topRoot(context), id);
    }

    /** "fallout1" or "fallout2" (anything else: the top root). */
    public static File baseRoot(Context context, String base) {
        return baseRoot(topRoot(context), base);
    }

    public static boolean isFallout2(Context context) {
        return FALLOUT2.equals(activeGameId(context));
    }

    // ---- pure helpers ----

    static boolean isKnown(String id) {
        if (id == null) return false;
        for (String known : IDS) if (known.equals(id)) return true;
        return false;
    }

    static String normalize(String id) {
        return isKnown(id) ? id : FALLOUT2;
    }

    static String activeGameId(File top) {
        if (top == null) return FALLOUT2;
        File file = new File(top, SELECTION_FILE);
        try (BufferedReader in = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line = in.readLine();
            return normalize(line);
        } catch (IOException e) {
            return FALLOUT2;
        }
    }

    static File gameRoot(File top, String id) {
        if (top == null) return null;
        String known = normalize(id);
        return FALLOUT2.equals(known) ? top : new File(new File(top, "games"), known);
    }

    static File baseRoot(File top, String base) {
        if (top == null) return null;
        return "fallout1".equals(base) ? new File(new File(top, "base"), "fallout1") : top;
    }
}
