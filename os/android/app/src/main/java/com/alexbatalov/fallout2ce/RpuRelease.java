package com.alexbatalov.fallout2ce;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

// Pure logic for "Install RPU" (no Android classes, unit tested): picking the
// release zip out of the GitHub API's JSON, the Range/resume arithmetic, the
// free-space check and the Wi-Fi decision.
final class RpuRelease {
    static final String API_URL = "https://api.github.com/repos/BGforgeNet/Fallout2_Restoration_Project/releases/latest";

    static final class Asset {
        final String name;
        final long size;
        final String url;

        Asset(String name, long size, String url) {
            this.name = name;
            this.size = size;
            this.url = url;
        }
    }

    final String tag;
    final Asset zip;

    RpuRelease(String tag, Asset zip) {
        this.tag = tag;
        this.zip = zip;
    }

    // "v2.4.34" -> "2.4.34".
    String version() {
        return tag.startsWith("v") || tag.startsWith("V") ? tag.substring(1) : tag;
    }

    // The mod's name in the list.
    String modName() {
        return "Restoration Project " + version();
    }

    // Files in the app's folder are named by tag and size, so a newer release
    // never resumes into an older release's partial file.
    String partName() {
        return "rpu-" + safe(tag) + "-" + zip.size + ".zip.part";
    }

    String zipName() {
        return "rpu-" + safe(tag) + "-" + zip.size + ".zip";
    }

    private static String safe(String text) {
        StringBuilder out = new StringBuilder();
        for (char ch : text.toCharArray()) {
            out.append(Character.isLetterOrDigit(ch) || ch == '.' || ch == '-' ? ch : '_');
        }
        return out.toString();
    }

    // The game zip: "rpu_v2.4.34.zip", not "..._translations.zip", the .exe
    // installer or the .torrent files. Null if the release has none.
    static RpuRelease parse(String json) throws IOException {
        Object root;
        try {
            root = new Json(json).value();
        } catch (RuntimeException e) {
            throw new IOException("bad release JSON", e);
        }
        if (!(root instanceof Map)) throw new IOException("bad release JSON");
        Map<?, ?> release = (Map<?, ?>) root;
        Object tag = release.get("tag_name");
        Object assets = release.get("assets");
        if (!(tag instanceof String) || !(assets instanceof List)) throw new IOException("bad release JSON");
        Asset best = null;
        for (Object item : (List<?>) assets) {
            if (!(item instanceof Map)) continue;
            Map<?, ?> map = (Map<?, ?>) item;
            Object name = map.get("name");
            Object size = map.get("size");
            Object url = map.get("browser_download_url");
            if (!(name instanceof String) || !(size instanceof Double) || !(url instanceof String)) continue;
            String lower = ((String) name).toLowerCase(Locale.ROOT);
            if (!lower.endsWith(".zip") || lower.contains("translation") || !lower.startsWith("rpu")) continue;
            long bytes = (long) (double) (Double) size;
            if (bytes <= 0 || !((String) url).startsWith("https://")) continue;
            if (best == null || bytes > best.size) best = new Asset((String) name, bytes, (String) url);
        }
        return best == null ? null : new RpuRelease((String) tag, best);
    }

    // The Range header to continue a partial file of [have] bytes, or null to
    // (re)start from the beginning.
    static String rangeHeader(long have, long total) {
        return have > 0 && have < total ? "bytes=" + have + "-" : null;
    }

    // What to do with the partial file: DONE (all of it is there), RESUME,
    // RESTART (empty or larger than the file: not ours).
    enum Resume { DONE, RESUME, RESTART }

    static Resume resume(long have, long total) {
        if (have == total && total > 0) return Resume.DONE;
        if (have > 0 && have < total) return Resume.RESUME;
        return Resume.RESTART;
    }

    // Whether the server honoured the Range request: 206 with
    // "Content-Range: bytes <have>-<last>/<total>" for exactly this file.
    static boolean rangeAccepted(int status, String contentRange, long have, long total) {
        if (status != 206 || contentRange == null) return false;
        String text = contentRange.trim().toLowerCase(Locale.ROOT);
        if (!text.startsWith("bytes ")) return false;
        String[] parts = text.substring(6).trim().split("[-/]");
        if (parts.length != 3) return false;
        try {
            return Long.parseLong(parts[0]) == have && Long.parseLong(parts[1]) == total - 1
                && Long.parseLong(parts[2]) == total;
        } catch (NumberFormatException e) {
            return false;
        }
    }

    // Free bytes needed before downloading: what is still to download, plus
    // the unpacking (ModInstall asks twice the unpacked size; the zip's files
    // are mostly stored, so about the zip's size, with 1/16 to spare).
    static long neededBytes(long total, long have) {
        long remaining = Math.max(0, total - Math.min(have, total));
        long unpacked = total + total / 16;
        return remaining + 2 * unpacked;
    }

    static boolean enoughSpace(long free, long total, long have) {
        return free >= neededBytes(total, have);
    }

    static long toMegabytes(long bytes) {
        return bytes / 1048576 + (bytes % 1048576 == 0 ? 0 : 1);
    }

    // Warn unless on an unmetered Wi-Fi or Ethernet connection.
    static boolean warnNotWifi(boolean wifiOrEthernet, boolean unmetered) {
        return !(wifiOrEthernet && unmetered);
    }

    // Minimal JSON reader (org.json is only a stub in JVM unit tests).
    // Numbers are Doubles; objects Maps; arrays Lists.
    private static final class Json {
        private final String text;
        private int pos;

        Json(String text) {
            this.text = text;
        }

        Object value() {
            Object result = read();
            skip();
            if (pos != text.length()) throw new IllegalArgumentException("trailing data");
            return result;
        }

        private void skip() {
            while (pos < text.length() && Character.isWhitespace(text.charAt(pos))) pos++;
        }

        private Object read() {
            skip();
            if (pos >= text.length()) throw new IllegalArgumentException("end");
            char ch = text.charAt(pos);
            if (ch == '{') {
                pos++;
                Map<String, Object> map = new java.util.HashMap<>();
                skip();
                if (text.charAt(pos) == '}') { pos++; return map; }
                while (true) {
                    skip();
                    String key = string();
                    skip();
                    expect(':');
                    map.put(key, read());
                    skip();
                    if (text.charAt(pos) == ',') { pos++; continue; }
                    expect('}');
                    return map;
                }
            }
            if (ch == '[') {
                pos++;
                List<Object> list = new ArrayList<>();
                skip();
                if (text.charAt(pos) == ']') { pos++; return list; }
                while (true) {
                    list.add(read());
                    skip();
                    if (text.charAt(pos) == ',') { pos++; continue; }
                    expect(']');
                    return list;
                }
            }
            if (ch == '"') return string();
            if (text.startsWith("true", pos)) { pos += 4; return Boolean.TRUE; }
            if (text.startsWith("false", pos)) { pos += 5; return Boolean.FALSE; }
            if (text.startsWith("null", pos)) { pos += 4; return null; }
            int start = pos;
            while (pos < text.length() && "+-0123456789.eE".indexOf(text.charAt(pos)) != -1) pos++;
            if (start == pos) throw new IllegalArgumentException("unexpected " + ch);
            return Double.valueOf(text.substring(start, pos));
        }

        private void expect(char ch) {
            if (pos >= text.length() || text.charAt(pos) != ch) throw new IllegalArgumentException("expected " + ch);
            pos++;
        }

        private String string() {
            expect('"');
            StringBuilder out = new StringBuilder();
            while (true) {
                char ch = text.charAt(pos++);
                if (ch == '"') return out.toString();
                if (ch != '\\') { out.append(ch); continue; }
                char esc = text.charAt(pos++);
                switch (esc) {
                case 'n': out.append('\n'); break;
                case 't': out.append('\t'); break;
                case 'r': out.append('\r'); break;
                case 'b': out.append('\b'); break;
                case 'f': out.append('\f'); break;
                case 'u': out.append((char) Integer.parseInt(text.substring(pos, pos + 4), 16)); pos += 4; break;
                default: out.append(esc);
                }
            }
        }
    }
}
