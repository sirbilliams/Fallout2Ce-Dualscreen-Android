package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.util.Log;
import android.view.Display;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Which display is the AYN Thor's bottom screen. Adapted from sirbilliams/neutron2-android
 * (BottomScreen.java), where it is proven on the Thor. The Thor has two built-in screens: the top
 * one is the default display (the game), the bottom one a built-in secondary display (the panels,
 * BottomScreen.java). Casting (screen mirroring,
 * Miracast / wireless display, USB-C HDMI) can add an external display that Android may also offer
 * as a presentation display; the bottom-screen views must never go there, so standard mirroring
 * keeps showing the game (the default display) on the TV.
 *
 * Only a built-in secondary display is chosen: never the default display, an invalid or private one,
 * a wireless / overlay / virtual one, or (once the built-in one has been seen and remembered) any
 * other display. The display type is read with the hidden Display.getType() (by reflection), else
 * parsed from Display.toString() ("type INTERNAL" ...), else guessed from the display's name.
 */
final class BottomDisplay {
    private static final String TAG = "BottomScreen";
    private static final String PREFS = "bottom_displays";
    private static final String KEY_BUILTIN = "builtin_secondary";
    static final String LOG_FILE = "display-log.txt";

    // android.view.Display.TYPE_* (hidden)
    static final int TYPE_UNKNOWN = 0, TYPE_INTERNAL = 1, TYPE_EXTERNAL = 2, TYPE_WIFI = 3,
            TYPE_OVERLAY = 4, TYPE_VIRTUAL = 5;

    private static String sLastLog;

    private BottomDisplay() {}

    /** The Thor's built-in bottom screen, or null (none, or only external / cast displays). */
    static Display find(Context context, DisplayManager displays) {
        return find(context, displays, null);
    }

    /** As find(); why = a short reason for the log line ("start", "display added" ...), or null for no log. */
    static Display find(Context context, DisplayManager displays, String why) {
        if (displays == null) return null;
        Display[] all;
        try {
            all = displays.getDisplays();
        } catch (RuntimeException e) {
            return null;
        }
        if (all == null) return null;
        SharedPreferences prefs = context != null
                ? context.getSharedPreferences(PREFS, Context.MODE_PRIVATE | Context.MODE_MULTI_PROCESS) : null;
        String remembered = prefs != null ? prefs.getString(KEY_BUILTIN, null) : null;

        Display byKey = null, internal = null, unknown = null;
        int unknownCount = 0;
        StringBuilder seen = new StringBuilder();
        for (Display d : all) {
            if (d == null) continue;
            int type = typeOf(d);
            String key = keyOf(d);
            boolean ok = usable(d, type);
            if (seen.length() > 0) seen.append("; ");
            seen.append(describe(d, type)).append(ok ? "" : " [skip]");
            if (!ok) continue;
            if (type == TYPE_INTERNAL || thorBottomSize(d)) { if (internal == null) internal = d; }
            else if (remembered != null && remembered.equals(key)) { if (byKey == null) byKey = d; }
            else { unknownCount++; if (unknown == null) unknown = d; }
        }
        // A built-in screen first, then the one remembered as built-in; a display of unknown type
        // only when no built-in screen has been seen before and it is the one candidate (the Thor
        // alone, no TV attached). A display reported EXTERNAL (HDMI) never gets here (usable()).
        Display pick = internal;
        if (pick == null) pick = byKey;
        if (pick == null && remembered == null && unknownCount == 1) pick = unknown;
        if (pick != null && prefs != null && !keyOf(pick).equals(remembered)) {
            prefs.edit().putString(KEY_BUILTIN, keyOf(pick)).apply();
        }
        if (why != null) {
            log(context, "displays (" + why + "): " + seen + " -> "
                    + (pick != null ? "bottom screen = display " + pick.getDisplayId() : "no bottom screen")
                    + (remembered != null ? " (remembered " + remembered + ")" : ""));
        }
        return pick;
    }

    /** Whether d could be the built-in bottom screen at all. */
    private static boolean usable(Display d, int type) {
        if (d.getDisplayId() == Display.DEFAULT_DISPLAY) return false;
        if (!d.isValid()) return false;
        if ((d.getFlags() & Display.FLAG_PRIVATE) != 0) return false;       // another app's virtual display
        // Exactly the Thor's panel size is the bottom screen, even as an overlay display (the CI emulator's
        // stand-in); never HDMI / external.
        if (type != TYPE_EXTERNAL && (d.getFlags() & Display.FLAG_PRESENTATION) != 0 && thorBottomSize(d)) return true;
        if (type == TYPE_WIFI || type == TYPE_OVERLAY || type == TYPE_VIRTUAL) return false;
        String name = d.getName() != null ? d.getName().toLowerCase(Locale.ROOT) : "";
        String[] cast = { "wifi", "wi-fi", "wireless", "miracast", "cast", "chromecast", "mirror",
                "overlay", "virtual", "screen record", "projection", "remote" };
        for (String c : cast) if (name.contains(c)) return false;
        if (type == TYPE_INTERNAL) return true;
        if (thorBottomSize(d)) return true;                  // the Thor's bottom panel, however it is reported
        if (type == TYPE_EXTERNAL) return false;             // HDMI / USB-C display
        String[] plug = { "hdmi", "displayport", "usb", "external", "monitor", "tv" };
        for (String c : plug) if (name.contains(c)) return false;
        return true;
    }

    /** The AYN Thor's bottom panel is 1240x1080 (a TV or monitor is not). */
    private static boolean thorBottomSize(Display d) {
        Point p = new Point();
        try {
            d.getRealSize(p);
        } catch (RuntimeException e) {
            return false;
        }
        return Math.min(p.x, p.y) == 1080 && Math.max(p.x, p.y) == 1240;
    }

    /** The hidden Display.getType(), else parsed from toString(), else TYPE_UNKNOWN. */
    static int typeOf(Display d) {
        try {
            Object t = Display.class.getMethod("getType").invoke(d);
            if (t instanceof Integer) return (Integer) t;
        } catch (Throwable e) {
            // hidden API refused: fall back to the description
        }
        String s;
        try {
            s = String.valueOf(d);
        } catch (RuntimeException e) {
            return TYPE_UNKNOWN;
        }
        int i = s.indexOf(", type ");
        if (i < 0) return TYPE_UNKNOWN;
        String t = s.substring(i + 7);
        if (t.startsWith("INTERNAL") || t.startsWith("BUILT_IN")) return TYPE_INTERNAL;
        if (t.startsWith("EXTERNAL") || t.startsWith("HDMI")) return TYPE_EXTERNAL;
        if (t.startsWith("WIFI")) return TYPE_WIFI;
        if (t.startsWith("OVERLAY")) return TYPE_OVERLAY;
        if (t.startsWith("VIRTUAL")) return TYPE_VIRTUAL;
        return TYPE_UNKNOWN;
    }

    /** A display's identity across runs (ids can change): its name and real size, short side first. */
    static String keyOf(Display d) {
        Point p = new Point();
        try {
            d.getRealSize(p);
        } catch (RuntimeException e) {
            // size unknown
        }
        return d.getName() + "|" + Math.min(p.x, p.y) + "x" + Math.max(p.x, p.y);
    }

    private static String describe(Display d, int type) {
        Point p = new Point();
        try {
            d.getRealSize(p);
        } catch (RuntimeException e) {
            // size unknown
        }
        String[] names = { "unknown", "internal", "external", "wifi", "overlay", "virtual" };
        int f = d.getFlags();
        return d.getDisplayId() + " \"" + d.getName() + "\" " + p.x + "x" + p.y + " "
                + (type >= 0 && type < names.length ? names[type] : "type" + type)
                + ((f & Display.FLAG_PRESENTATION) != 0 ? " presentation" : "")
                + ((f & Display.FLAG_PRIVATE) != 0 ? " private" : "")
                + ((f & Display.FLAG_SECURE) != 0 ? " secure" : "")
                + (d.isValid() ? "" : " invalid");
    }

    /** One line to logcat and to <files>/display-log.txt (for reports of a wrong screen). */
    static void log(Context context, String line) {
        if (line.equals(sLastLog)) return;                   // the same displays, the same choice
        sLastLog = line;
        Log.i(TAG, line);
        File dir = context != null ? context.getExternalFilesDir(null) : null;
        if (dir == null) return;
        File f = new File(dir, LOG_FILE);
        boolean append = f.length() < 32 * 1024;                // keep it small: start afresh when full
        String stamp = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new Date());
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(f, append);
            out.write((stamp + " " + line + "\n").getBytes("UTF-8"));
        } catch (IOException e) {
            // no log then
        } finally {
            if (out != null) try { out.close(); } catch (IOException e) { /* ignore */ }
        }
    }
}
