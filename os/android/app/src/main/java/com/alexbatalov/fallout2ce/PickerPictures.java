package com.alexbatalov.fallout2ce;

import java.io.*;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.net.URLEncoder;
import java.util.regex.*;

// Pure source/cache policy. No pictures or network access at build time.
final class PickerPictures {
    static final int PREVIEW_CAP = 4 * 1024 * 1024;
    static final int FULL_CAP = 8 * 1024 * 1024;
    static final int PAGE_CAP = 1024 * 1024;

    static File directory(File top, String id, boolean installed) {
        if (top == null || !id.matches("fallout2|ettu|sonora|resurrection|nevada|olympus2207|megamod|yesterday"))
            throw new IllegalArgumentException("unknown game");
        return new File(top, installed ? "games/" + id + "/picker" : "picker-preview/" + id);
    }

    static File cached(File top, String id) {
        File full = new File(directory(top, id, true), "art.rgba");
        File preview = new File(directory(top, id, false), "art.rgba");
        return full.isFile() ? full : preview.isFile() ? preview : null;
    }

    /**
     * Art made before social cards were rejected: when the picture directory's
     * credit.txt "Image:" line is a GitHub social card, removes art.rgba and
     * credit.txt (a regenerable cache) so the picture is fetched again, or the
     * plain themed panel shows when no replacement exists. True when purged.
     */
    static boolean purgeSocialCard(File dir) {
        File art = new File(dir, "art.rgba"), credit = new File(dir, "credit.txt");
        if (!art.isFile() || !credit.isFile()) return false;
        try {
            for (String line : new String(java.nio.file.Files.readAllBytes(credit.toPath()), StandardCharsets.UTF_8).split("\\r?\\n")) {
                if (!line.startsWith("Image:")) continue;
                if (!isSocialCard(line.substring(6).trim())) return false;
                return art.delete() & credit.delete();
            }
        } catch (IOException e) { /* unreadable credit: keep the art */ }
        return false;
    }

    static boolean mayFetch(boolean connected, boolean wifiOrEthernet, boolean metered) {
        return connected && wifiOrEthernet && !metered;
    }

    static String readmeApi(String page) {
        URI uri = URI.create(page);
        if (!"github.com".equalsIgnoreCase(uri.getHost())) return null;
        String[] parts = uri.getPath().split("/");
        if (parts.length < 3 || !parts[1].matches("[A-Za-z0-9_.-]+") || !parts[2].matches("[A-Za-z0-9_.-]+")) return null;
        return "https://api.github.com/repos/" + parts[1] + "/" + parts[2] + "/readme";
    }

    // GitHub README API response; only download_url is used, never avatar URLs.
    static String readmeUrl(String json) {
        Matcher m = Pattern.compile("\\\"download_url\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"\\\\])*)\\\"").matcher(json);
        if (!m.find()) return null;
        String value = m.group(1).replace("\\/", "/").replace("\\u0026", "&");
        return safeUrl("https://github.com/", value);
    }

    /** GitHub's generated repository social cards and avatars are not game art. */
    static boolean isSocialCard(String url) {
        try {
            String host = URI.create(url).getHost();
            return host != null && (host.equalsIgnoreCase("opengraph.githubassets.com")
                || host.equalsIgnoreCase("repository-images.githubusercontent.com")
                || host.equalsIgnoreCase("avatars.githubusercontent.com"));
        } catch (IllegalArgumentException e) { return true; }
    }

    static String safeUrl(String page, String value) {
        try {
            URI uri = URI.create(page).resolve(value.trim());
            String host = uri.getHost();
            if (!"https".equalsIgnoreCase(uri.getScheme()) || host == null || uri.getUserInfo() != null
                    || (uri.getPort() != -1 && uri.getPort() != 443)
                    || host.equalsIgnoreCase("localhost") || host.indexOf(':') >= 0
                    || host.matches("[0-9.]+") || host.endsWith(".local")) return null;
            return uri.toASCIIString();
        } catch (IllegalArgumentException e) { return null; }
    }

    static String imageUrl(String page, String text) {
        Matcher tags = Pattern.compile("<meta\\b[^>]*>", Pattern.CASE_INSENSITIVE).matcher(text);
        while (tags.find()) {
            String property = null, content = null;
            Matcher attrs = Pattern.compile("([\\w:-]+)\\s*=\\s*(['\"])(.*?)\\2", Pattern.DOTALL).matcher(tags.group());
            while (attrs.find()) {
                if (attrs.group(1).equalsIgnoreCase("property") || attrs.group(1).equalsIgnoreCase("name")) property = attrs.group(3);
                if (attrs.group(1).equalsIgnoreCase("content")) content = attrs.group(3);
            }
            if ("og:image".equalsIgnoreCase(property) && content != null) {
                String url = safeUrl(page, content.replace("&amp;", "&").replace("&#38;", "&"));
                if (url != null && !isSocialCard(url)) return url;
            }
        }
        // README image URLs, relative to the raw README URL returned by GitHub.
        Matcher md = Pattern.compile("!\\[[^\\]]*\\]\\(<?([^\\s)>]+)>?(?:\\s+[^)]*)?\\)").matcher(text);
        if (md.find()) return safeUrl(page, md.group(1));
        Matcher img = Pattern.compile("<img\\b[^>]*\\bsrc\\s*=\\s*['\"]([^'\"]+)['\"]", Pattern.CASE_INSENSITIVE).matcher(text);
        return img.find() ? safeUrl(page, img.group(1)) : null;
    }

    /** Names that are never game art: badges, shields, icons, logos, avatars, social cards, vector/animated files. */
    static boolean unwantedImage(String url) {
        if (url == null || isSocialCard(url)) return true;
        String lower = url.toLowerCase(Locale.ROOT);
        String path = lower.replaceAll("[?#].*$", "");
        String host = "";
        try { host = URI.create(url).getHost(); if (host == null) host = ""; } catch (IllegalArgumentException e) { return true; }
        host = host.toLowerCase(Locale.ROOT);
        if (host.contains("shields.io") || host.contains("badge") || host.endsWith("travis-ci.org") || host.contains("codecov")
            || host.contains("opengraph") || host.contains("gravatar")) return true;
        if (path.endsWith(".svg") || path.endsWith(".gif") || path.endsWith(".ico") || path.endsWith(".webp")) return true;
        return path.matches(".*(badge|shield|icon|logo|avatar|favicon|button|sponsor|social|banner|title|emoji).*");
    }

    /**
     * Every acceptable image URL on the page, best guess first: og:image, then the
     * README's markdown and HTML images in document order. At most [max].
     */
    static java.util.List<String> imageUrls(String page, String text, int max) {
        java.util.LinkedHashSet<String> urls = new java.util.LinkedHashSet<>();
        Matcher tags = Pattern.compile("<meta\\b[^>]*>", Pattern.CASE_INSENSITIVE).matcher(text);
        while (tags.find()) {
            String property = null, content = null;
            Matcher attrs = Pattern.compile("([\\w:-]+)\\s*=\\s*(['\"])(.*?)\\2", Pattern.DOTALL).matcher(tags.group());
            while (attrs.find()) {
                if (attrs.group(1).equalsIgnoreCase("property") || attrs.group(1).equalsIgnoreCase("name")) property = attrs.group(3);
                if (attrs.group(1).equalsIgnoreCase("content")) content = attrs.group(3);
            }
            if ("og:image".equalsIgnoreCase(property) && content != null) {
                String url = safeUrl(page, content.replace("&amp;", "&").replace("&#38;", "&"));
                if (!unwantedImage(url)) urls.add(url);
            }
        }
        Matcher md = Pattern.compile("!\\[[^\\]]*\\]\\(<?([^\\s)>]+)>?(?:\\s+[^)]*)?\\)|<img\\b[^>]*\\bsrc\\s*=\\s*['\"]([^'\"]+)['\"]",
            Pattern.CASE_INSENSITIVE).matcher(text);
        while (md.find() && urls.size() < max) {
            String url = safeUrl(page, md.group(1) != null ? md.group(1) : md.group(2));
            if (!unwantedImage(url)) urls.add(url);
        }
        java.util.List<String> list = new java.util.ArrayList<>(urls);
        return list.size() > max ? list.subList(0, max) : list;
    }

    static final int RANK_VERSION = 3;
    static final int CANDIDATE_CAP = 16;
    static final long NONE_BACKOFF_MS = 24L * 60 * 60 * 1000;

    static String rankVersion(String appVersion) { return RANK_VERSION + ":" + appVersion; }

    static boolean needsRanking(File dir, String version) {
        if (!new File(dir, "art.rgba").isFile()) return true;
        try {
            return !version.equals(utf8(java.nio.file.Files.readAllBytes(new File(dir, "rank-version").toPath())));
        } catch (IOException e) { return true; }
    }

    /** Pure ranking; every accepted online image outranks game-file fallback. */
    static long score(String name, String mime, int width, int height, double richness, boolean gameFile) {
        if (gameFile) return width > 0 && height > 0 ? 0 : -1;
        if (unwantedImage(name) || !("image/png".equals(mime) || "image/jpeg".equals(mime))) return -1;
        long area = imageScore(width, height);
        if (area < 0) return -1;
        double bonus = name.toLowerCase(Locale.ROOT).matches(".*(screen|screenshot|scr|gameplay|cover|art|wallpaper).*") ? 1.5 : 1;
        double colour = Double.isFinite(richness) && richness >= 25 ? 1 : 0.25;
        return Math.max(1, (long) (area * bonus * colour));
    }

    static long imageScore(int width, int height) {
        if (width < 960 || height <= 0 || width > 16384 || height > 16384 || (long) width * height > 64000000) return -1;
        double aspect = width / (double) height;
        return aspect < 1.3 || aspect > 2.2 ? -1 : (long) width * height;
    }

    // A title is a discovery hint, never a fixed picture selection. Missing pages yield nothing.
    static String wikiTitle(String id) {
        String[][] titles = {
            { "fallout2", "Fallout 2" }, { "ettu", "Fallout Et Tu" },
            { "sonora", "Fallout: Sonora" }, { "resurrection", "Fallout 1.5: Resurrection" },
            { "nevada", "Fallout: Nevada" }, { "olympus2207", "Olympus 2207" },
            { "megamod", "MIB88 Megamod" }, { "yesterday", "Fallout: Yesterday" }
        };
        for (String[] row : titles) if (row[0].equals(id)) return row[1];
        return "";
    }

    static String encode(String value) {
        try { return URLEncoder.encode(value, "UTF-8"); }
        catch (UnsupportedEncodingException impossible) { throw new AssertionError(impossible); }
    }

    static String wikiApi(String id) {
        return "https://fallout.fandom.com/api.php?action=query&format=json&prop=pageimages%7Cimages"
            + "&piprop=original&imlimit=12&redirects=1&titles=" + encode(wikiTitle(id));
    }

    static Map<?, ?> object(Object value) {
        return value instanceof Map ? (Map<?, ?>) value : Collections.emptyMap();
    }

    static List<?> array(Object value) {
        return value instanceof List ? (List<?>) value : Collections.emptyList();
    }

    static List<?> pages(String json) {
        Object pages = object(object(new RpuRelease.Json(json).value()).get("query")).get("pages");
        return pages instanceof Map ? new ArrayList<>(object(pages).values()) : array(pages);
    }

    static void addImage(Set<String> out, Object value) {
        if (!(value instanceof String)) return;
        String url = safeUrl("https://fallout.fandom.com/", (String) value);
        if (!unwantedImage(url)) out.add(url);
    }

    static List<String> wikiImages(String json) {
        Set<String> out = new LinkedHashSet<>();
        for (Object page : pages(json)) {
            Map<?, ?> p = object(page);
            if (p.containsKey("missing")) continue;
            addImage(out, object(p.get("original")).get("source"));
            for (Object info : array(p.get("imageinfo"))) {
                Map<?, ?> i = object(info);
                if (!(i.get("width") instanceof Number) || !(i.get("height") instanceof Number)) continue;
                String url = String.valueOf(i.get("url"));
                if (score(url, String.valueOf(i.get("mime")), ((Number) i.get("width")).intValue(),
                        ((Number) i.get("height")).intValue(), 100, false) >= 0) addImage(out, url);
            }
        }
        return new ArrayList<>(out);
    }

    static String wikiImageInfoApi(String json) {
        List<String> titles = new ArrayList<>();
        for (Object page : pages(json)) {
            Map<?, ?> p = object(page);
            if (p.containsKey("missing")) continue;
            for (Object image : array(p.get("images"))) {
                Object title = object(image).get("title");
                if (title instanceof String && titles.size() < 12
                        && !unwantedImage("https://fallout.fandom.com/" + encode((String) title))) titles.add((String) title);
            }
        }
        return titles.isEmpty() ? null : "https://fallout.fandom.com/api.php?action=query&format=json&prop=imageinfo&iiprop=url%7Csize%7Cmime&titles="
            + encode(String.join("|", titles));
    }

    static List<String> githubImages(String json, boolean releases) {
        Set<String> out = new LinkedHashSet<>();
        List<?> entries = array(new RpuRelease.Json(json).value());
        int count = 0;
        for (Object entry : entries) {
            if (++count > (releases ? 2 : 100)) break;
            for (Object item : releases ? array(object(entry).get("assets")) : Collections.singletonList(entry)) {
                Map<?, ?> file = object(item);
                if (!releases && !"file".equals(file.get("type"))) continue;
                String name = String.valueOf(file.get("name")).toLowerCase(Locale.ROOT);
                if (!name.matches(".*\\.(png|jpe?g)") || unwantedImage("https://github.com/" + encode(name))) continue;
                addImage(out, file.get(releases ? "browser_download_url" : "download_url"));
                if (out.size() >= 12) return new ArrayList<>(out);
            }
        }
        return new ArrayList<>(out);
    }

    // ---- none-found backoff ----

    /** True while a "none-found" marker younger than NONE_BACKOFF_MS exists (wall-clock millis). */
    static boolean noneBackoffActive(File dir, long nowMs) {
        File marker = new File(dir, "none-found");
        if (!marker.isFile()) return false;
        try {
            long at = Long.parseLong(utf8(java.nio.file.Files.readAllBytes(marker.toPath())).trim());
            return at <= nowMs && nowMs - at < NONE_BACKOFF_MS;
        } catch (IOException | NumberFormatException e) { return false; }
    }

    static void writeNone(File dir, long nowMs) throws IOException {
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("picture directory");
        java.nio.file.Files.write(new File(dir, "none-found").toPath(), Long.toString(nowMs).getBytes(StandardCharsets.UTF_8));
    }

    // ---- repository image folders ----

    private static final Set<String> IMAGE_FOLDERS = new HashSet<>(Arrays.asList(
        "docs", "screenshots", "screens", "gallery", "images", "img", "media", "art", "pictures"));

    /**
     * Folders worth listing, from a GitHub contents listing of the repository root. Matching is
     * case-insensitive; "Resources" is searched one level down for "Gallery"/"Images" (the Fo1in2
     * layout, Resources/Gallery). Returns repository-relative paths with the real casing, at most 6.
     */
    static List<String> imageFolders(String rootJson) {
        List<String> out = new ArrayList<>();
        try {
            for (Object entry : array(new RpuRelease.Json(rootJson).value())) {
                Map<?, ?> e = object(entry);
                if (!"dir".equals(e.get("type")) || !(e.get("name") instanceof String)) continue;
                String name = (String) e.get("name");
                if (!name.matches("[A-Za-z0-9_. -]+")) continue;
                String lower = name.toLowerCase(Locale.ROOT);
                if (IMAGE_FOLDERS.contains(lower)) out.add(name);
                else if (lower.equals("resources") || lower.equals("assets")) { out.add(name + "/Gallery"); out.add(name + "/Images"); }
                if (out.size() >= 6) break;
            }
        } catch (RuntimeException ignored) { }
        return out;
    }

    /** Encodes each path segment for the GitHub contents API. */
    static String contentsPath(String folder) {
        StringBuilder sb = new StringBuilder();
        for (String part : folder.split("/")) sb.append('/').append(encode(part).replace("+", "%20"));
        return sb.toString();
    }

    // ---- ModDB screenshots ----

    static String moddbSlug(String id) {
        switch (id) {
            case "megamod": return "mib88-megamod";
            case "resurrection": return "fallout-15-resurrection";
            case "olympus2207": return "olympus-2207";
            default: return null; // no confirmed ModDB page (ettu, sonora, nevada, yesterday; "f3-van-buren" is another project)
        }
    }

    static String moddbImagesPage(String id) {
        String slug = moddbSlug(id);
        return slug == null ? null : "https://www.moddb.com/mods/" + slug + "/images";
    }

    /**
     * Full-size screenshots from a ModDB images page. Takes media.moddb.com image URLs; a cached
     * thumbnail (media.moddb.com/cache/images/.../thumb_NNN/name.jpg) is rewritten to its full-size
     * original (media.moddb.com/images/.../name.jpg). Member avatars, icons and site chrome are
     * skipped. Document order, duplicates removed, at most [max].
     */
    static List<String> moddbImages(String html, int max) {
        Set<String> out = new LinkedHashSet<>();
        Matcher m = Pattern.compile("https?://media\\.moddb\\.com/(?:cache/)?images/[^\"'\\s<>)]+\\.(?:png|jpe?g)", Pattern.CASE_INSENSITIVE).matcher(html);
        while (m.find() && out.size() < max) {
            String url = m.group().replace("&amp;", "&");
            String lower = url.toLowerCase(Locale.ROOT);
            if (lower.contains("/members/") || lower.contains("/avatar") || lower.contains("/icons/") || lower.contains("/groups/")
                || lower.contains("/articles/") || lower.contains("/downloads/") || lower.contains("/thumbsquare")
                || lower.contains("/thumb_100x") || lower.contains("/thumb_75x") || lower.contains("/thumb_150x")) continue;
            String full = url.replaceFirst("(?i)media\\.moddb\\.com/cache/images/(.*)/thumb[a-z]*_[0-9x]+/", "media.moddb.com/images/$1/");
            if (full.toLowerCase(Locale.ROOT).contains("/cache/")) continue; // thumbnail we cannot map back
            if (unwantedImage(full)) continue;
            out.add(full);
        }
        return new ArrayList<>(out);
    }

    static byte[] readLimited(InputStream input, int cap) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int n;
        while ((n = input.read(buffer, 0, Math.min(buffer.length, cap - out.size() + 1))) != -1) {
            if (n > cap - out.size()) throw new IOException("picture response exceeds cap");
            out.write(buffer, 0, n);
        }
        return out.toByteArray();
    }

    static String utf8(byte[] bytes) { return new String(bytes, StandardCharsets.UTF_8); }
}
