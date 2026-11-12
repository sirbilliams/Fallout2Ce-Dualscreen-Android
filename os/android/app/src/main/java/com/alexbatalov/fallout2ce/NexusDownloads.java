package com.alexbatalov.fallout2ce;

import java.io.File;
import java.io.IOException;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

// Pure logic of the Nexus browser's downloads: file names, the index of
// "Downloaded, not installed" files and which of them to ask about.
final class NexusDownloads {
    static final String INDEX = "downloads.tsv";

    static final class Entry {
        final String file;   // name inside the downloads folder
        final String title;  // mod name shown in the question

        Entry(String file, String title) {
            this.file = file;
            this.title = title;
        }
    }

    // True for a URL whose path is an archive (CDN redirects).
    static boolean isArchiveUrl(String url) {
        if (url == null) return false;
        int cut = url.length();
        for (char c : new char[] { '?', '#' }) {
            int i = url.indexOf(c);
            if (i >= 0 && i < cut) cut = i;
        }
        String path = url.substring(0, cut).toLowerCase(Locale.ROOT);
        return path.endsWith(".zip") || path.endsWith(".7z") || path.endsWith(".rar");
    }

    // Content-Disposition (filename*= wins over filename=), else the URL's
    // last path segment, else "mod.zip". Never contains a path separator.
    static String fileName(String contentDisposition, String url) {
        String name = fromDisposition(contentDisposition);
        if (name == null && url != null) {
            int cut = url.length();
            for (char c : new char[] { '?', '#' }) {
                int i = url.indexOf(c);
                if (i >= 0 && i < cut) cut = i;
            }
            String path = url.substring(0, cut);
            name = decode(path.substring(path.lastIndexOf('/') + 1));
        }
        return sanitize(name);
    }

    private static String fromDisposition(String cd) {
        if (cd == null) return null;
        String lower = cd.toLowerCase(Locale.ROOT);
        int star = lower.indexOf("filename*=");
        if (star >= 0) {
            String v = value(cd, star + 10);
            int quote = v.indexOf("''");
            if (quote >= 0) {
                String charset = v.substring(0, quote);
                String text = v.substring(quote + 2);
                try {
                    return URLDecoder.decode(text.replace("+", "%2B"), charset.isEmpty() ? "UTF-8" : charset);
                } catch (Exception ignored) {
                    return decode(text);
                }
            }
        }
        int plain = lower.indexOf("filename=");
        while (plain > 0 && lower.charAt(plain - 1) == '*') plain = lower.indexOf("filename=", plain + 1);
        if (plain >= 0) return value(cd, plain + 9);
        return null;
    }

    private static String value(String cd, int from) {
        String rest = cd.substring(from).trim();
        if (rest.startsWith("\"")) {
            int end = rest.indexOf('"', 1);
            return end < 0 ? rest.substring(1) : rest.substring(1, end);
        }
        int end = rest.indexOf(';');
        return (end < 0 ? rest : rest.substring(0, end)).trim();
    }

    private static String decode(String s) {
        try {
            return URLDecoder.decode(s.replace("+", "%2B"), "UTF-8");
        } catch (Exception e) {
            return s;
        }
    }

    static String sanitize(String name) {
        if (name == null) return "mod.zip";
        name = name.replace('\\', '/');
        name = name.substring(name.lastIndexOf('/') + 1).replaceAll("[\\p{Cntrl}:*?\"<>|]", "_").trim();
        while (name.startsWith(".")) name = name.substring(1);
        return name.isEmpty() ? "mod.zip" : name;
    }

    // A name not yet used in [existing]: "a.zip" -> "a-2.zip".
    static String uniqueFile(List<String> existing, String wanted) {
        if (!existing.contains(wanted)) return wanted;
        int dot = wanted.lastIndexOf('.');
        String stem = dot > 0 ? wanted.substring(0, dot) : wanted;
        String ext = dot > 0 ? wanted.substring(dot) : "";
        for (int n = 2; ; n++) {
            String candidate = stem + "-" + n + ext;
            if (!existing.contains(candidate)) return candidate;
        }
    }

    // "Foo Mod at Fallout 2 Nexus - Mods and community" -> "Foo Mod".
    static String modName(String pageTitle, String fileName) {
        String t = pageTitle == null ? "" : pageTitle.trim();
        int at = t.lastIndexOf(" at Fallout 2 Nexus");
        if (at > 0) t = t.substring(0, at).trim();
        if (t.isEmpty() || t.toLowerCase(Locale.ROOT).startsWith("http")) {
            int dot = fileName.lastIndexOf('.');
            t = dot > 0 ? fileName.substring(0, dot) : fileName;
        }
        return t;
    }

    // One line per entry: file TAB title.
    static List<Entry> read(File folder) throws IOException {
        List<Entry> entries = new ArrayList<>();
        File index = new File(folder, INDEX);
        if (!index.isFile()) return entries;
        for (String line : Files.readAllLines(index.toPath(), StandardCharsets.UTF_8)) {
            int tab = line.indexOf('\t');
            if (tab <= 0) continue;
            String file = sanitize(line.substring(0, tab));
            entries.add(new Entry(file, line.substring(tab + 1)));
        }
        return entries;
    }

    static void write(File folder, List<Entry> entries) throws IOException {
        StringBuilder text = new StringBuilder();
        for (Entry e : entries) {
            text.append(e.file).append('\t').append(e.title.replace('\t', ' ').replace('\n', ' ')).append('\n');
        }
        File temp = new File(folder, INDEX + ".tmp");
        Files.write(temp.toPath(), text.toString().getBytes(StandardCharsets.UTF_8));
        Files.move(temp.toPath(), new File(folder, INDEX).toPath(),
            java.nio.file.StandardCopyOption.REPLACE_EXISTING);
    }

    // Entries to ask about: indexed and the file is still there.
    static List<Entry> pending(File folder) throws IOException {
        List<Entry> asked = new ArrayList<>();
        for (Entry e : read(folder)) {
            if (new File(folder, e.file).isFile()) asked.add(e);
        }
        return asked;
    }

    static void add(File folder, String file, String title) throws IOException {
        List<Entry> entries = read(folder);
        entries.removeIf(e -> e.file.equals(file));
        entries.add(new Entry(file, title));
        write(folder, entries);
    }

    static void remove(File folder, String file) throws IOException {
        List<Entry> entries = read(folder);
        entries.removeIf(e -> e.file.equals(file));
        write(folder, entries);
    }
}
